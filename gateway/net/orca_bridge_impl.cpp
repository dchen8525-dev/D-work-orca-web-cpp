#include <orca/gateway/net/orca_bridge_impl.hpp>

#include <orca/gateway/net/orca_rpc_client.hpp>
#include <orca/protocol/box_crypto.hpp>

#include <boost/asio/post.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace orca::gateway {

namespace {

constexpr std::uint64_t kRetryBaseMs = 500;
constexpr std::uint64_t kRetryMaxMs = 30'000;
constexpr std::uint64_t kDescriptorTimeoutMs = 10'000;

std::mt19937& generator() {
    static thread_local std::mt19937 engine{std::random_device{}()};
    return engine;
}

std::uint64_t backoffMs(int attempt) {
    const double exponential =
        std::min<double>(static_cast<double>(kRetryMaxMs),
                         static_cast<double>(kRetryBaseMs) * std::pow(2.0, std::min(attempt, 10)));
    const double jitter = std::uniform_real_distribution<>(0.0, 1.0)(generator());
    return static_cast<std::uint64_t>(std::llround(exponential * (0.8 + jitter * 0.4)));
}

bool isString(const nlohmann::json& object, const char* key) {
    return object.is_object() && object.contains(key) && object.at(key).is_string();
}

// A fresh request id per call (randomUUID() in rpc-client.ts).
std::string newRequestId() {
    std::string id;
    if (const auto bytes = orca::protocol::randomBytes(16)) {
        static constexpr char kHex[] = "0123456789abcdef";
        id.reserve(bytes->size() * 2);
        for (const auto byte : *bytes) {
            id.push_back(kHex[byte >> 4]);
            id.push_back(kHex[byte & 0xf]);
        }
    }
    return id.empty() ? "orca-bridge-request" : id;
}

}  // namespace

OrcaBridgeImpl::OrcaBridgeImpl(OrcaBridgeOptions options,
                               std::function<void(const std::string&)> log)
    : options_(std::move(options)),
      log_(std::move(log)),
      work_(io_.get_executor()) {}

OrcaBridgeImpl::~OrcaBridgeImpl() { shutdown(); }

void OrcaBridgeImpl::start() {
    // Start at most once. Unlike the hub client this must not join a worker:
    // start() never returns early by joining, it only guards against a second
    // thread being spawned on the same io_context.
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) return;
    worker_ = std::thread([this] { io_.run(); });
    boost::asio::post(io_, [this] {
        if (!stopped_) connectOnce();
    });
}

void OrcaBridgeImpl::shutdown() {
    if (stopped_.exchange(true)) return;
    if (!worker_.joinable()) {
        io_.stop();
        return;
    }
    boost::asio::post(io_, [this] {
        if (connection_) connection_->close("gateway stopping");
        if (retryTimer_) {
            retryTimer_->cancel();
            retryTimer_.reset();
        }
        work_.reset();
    });
    worker_.join();
}

void OrcaBridgeImpl::connectOnce() {
    if (stopped_) return;
    setState(OrcaState::kConnecting, {});

    OrcaConnection::Options options;
    options.endpoint = options_.endpoint;
    options.deviceToken = options_.deviceToken;
    options.hostPublicKeyB64 = options_.hostPublicKeyB64;
    options.transport = options_.transport;
    options.relayHostId = options_.relayHostId;

    const OrcaConnection::CloseSink onClose = [this](std::optional<LinkError> error) {
        onConnectionClosed(std::move(error));
    };
    const OrcaConnection::ConnectDone onConnected =
        [this](OrcaConnection::Ptr connection, std::optional<LinkError> error) {
            if (stopped_) {
                if (connection) connection->close("bridge stopping");
                return;
            }
            if (error) {
                onConnectFailed(*error);
                return;
            }
            connection_ = std::move(connection);
            attempt_ = 0;
            log("orca connected: " + options_.endpoint);
            setState(OrcaState::kConnected, {});
            readDescriptor();
            replaySubscriptions();
        };
    OrcaConnection::connect(io_, std::move(options), std::move(onClose),
                            std::move(onConnected));
}

void OrcaBridgeImpl::onConnectFailed(const LinkError& error) {
    if (stopped_) return;
    connection_.reset();
    log(std::string("orca connect failed: ") + error.what());
    setState(OrcaState::kDisconnected, error.what());
    scheduleRetry();
}

void OrcaBridgeImpl::onConnectionClosed(std::optional<LinkError> error) {
    if (stopped_) return;
    // The old socket's streams are gone (connection_.reset() also hands the
    // hub every pending request an error). Subscriptions stay registered and
    // replay on the next socket.
    connection_.reset();
    if (error) {
        log(std::string("orca disconnected: ") + error->what());
        setState(OrcaState::kDisconnected, error->what());
    } else {
        log("orca disconnected");
        setState(OrcaState::kDisconnected, {});
    }
    scheduleRetry();
}

void OrcaBridgeImpl::scheduleRetry() {
    if (stopped_) return;
    const std::uint64_t delay = backoffMs(attempt_);
    ++attempt_;
    log("orca retry in " + std::to_string(delay) + " ms");
    if (retryTimer_) {
        retryTimer_->cancel();
        retryTimer_.reset();
    }
    retryTimer_.emplace(io_, std::chrono::milliseconds(delay));
    retryTimer_->async_wait([this](boost::system::error_code ec) {
        if (ec || stopped_) return;
        connectOnce();
    });
}

void OrcaBridgeImpl::readDescriptor() {
    if (!connection_) return;
    connection_->request(
        newRequestId(), "status.get", nlohmann::json(), kDescriptorTimeoutMs,
        [this](std::optional<nlohmann::json> result, std::optional<LinkError> error) {
            if (stopped_ || error || !result || !result->is_object()) return;
            nlohmann::json descriptor = nlohmann::json::object();
            if (isString(*result, "appVersion")) {
                descriptor["appVersion"] = result->at("appVersion");
            }
            if (result->contains("runtimeProtocolVersion") &&
                result->at("runtimeProtocolVersion").is_number_integer()) {
                descriptor["protocolVersion"] = result->at("runtimeProtocolVersion");
            }
            if (isString(*result, "hostPlatform")) {
                descriptor["hostPlatform"] = result->at("hostPlatform");
            }
            if (isString(*result, "machineName")) {
                descriptor["machineName"] = result->at("machineName");
            }
            {
                std::lock_guard lock(mutex_);
                descriptor_ = descriptor;
            }
            BridgeEvent event;
            event.kind = BridgeEvent::Kind::kOrca;
            event.payload = std::move(descriptor);
            publish(event);
        });
}

void OrcaBridgeImpl::replaySubscriptions() {
    for (const auto& [id, subscription] : subscriptions_) {
        // The old stream ended with the previous socket: tell the hub before
        // the new frames.
        BridgeEvent reset;
        reset.kind = BridgeEvent::Kind::kStreamReset;
        reset.id = id;
        publish(reset);
        openSubscription(id, subscription);
    }
}

void OrcaBridgeImpl::openSubscription(const std::string& id,
                                      const Subscription& subscription) {
    if (!connection_ || !connection_->open()) return;
    connection_->subscribe(id, subscription.method, subscription.params,
                           [this, id, method = subscription.method](const nlohmann::json& frame) {
                               if (stopped_) return;
                               // A dropped Orca socket leaves its streams dead;
                               // never leak a stale stream into a newer
                               // subscription of the same id.
                               const auto it = subscriptions_.find(id);
                               if (it == subscriptions_.end() || it->second.method != method) {
                                   return;
                               }
                               BridgeEvent event;
                               event.id = id;
                               if (frame.is_object() && frame.value("type", "") == "error") {
                                   event.kind = BridgeEvent::Kind::kStreamError;
                                   if (frame.contains("error") && frame.at("error").is_object()) {
                                       event.payload = frame.at("error");
                                   } else {
                                       event.payload = nlohmann::json{
                                           {"code", "remote_runtime_unavailable"},
                                           {"message", "Orca stream error"}};
                                   }
                               } else {
                                   event.kind = BridgeEvent::Kind::kStreamFrame;
                                   event.payload = frame;
                               }
                               publish(event);
                           });
}

void OrcaBridgeImpl::setState(OrcaState state, std::string message) {
    {
        std::lock_guard lock(mutex_);
        if (state_ == state && message.empty()) return;
        state_ = state;
    }
    BridgeEvent event;
    event.kind = BridgeEvent::Kind::kState;
    event.state = orcaStateName(state);
    event.message = std::move(message);
    publish(event);
}

void OrcaBridgeImpl::publish(const BridgeEvent& event) {
    EventSink sink;
    {
        std::lock_guard lock(mutex_);
        sink = sink_;
    }
    if (sink) sink(event);
}

void OrcaBridgeImpl::log(const std::string& message) const {
    if (log_) log_(message);
}

void OrcaBridgeImpl::setEventSink(EventSink sink) {
    std::lock_guard lock(mutex_);
    sink_ = std::move(sink);
}

OrcaState OrcaBridgeImpl::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

nlohmann::json OrcaBridgeImpl::descriptor() const {
    std::lock_guard lock(mutex_);
    return descriptor_;
}

void OrcaBridgeImpl::request(const std::string& id, const std::string& method,
                             const nlohmann::json& params, std::uint64_t timeoutMs,
                             RequestDone done) {
    boost::asio::post(io_, [this, id, method, params, timeoutMs,
                            done = std::move(done)]() mutable {
        if (stopped_) {
            if (done) {
                done(std::nullopt, LinkError("remote_runtime_unavailable", "Gateway is stopping"));
            }
            return;
        }
        if (!connection_ || !connection_->open()) {
            if (done) {
                done(std::nullopt,
                     LinkError("remote_runtime_unavailable", "Desktop Orca is not connected"));
            }
            return;
        }
        connection_->request(id, method, params, timeoutMs, std::move(done));
    });
}

void OrcaBridgeImpl::subscribe(const std::string& id, const std::string& method,
                               const nlohmann::json& params) {
    boost::asio::post(io_, [this, id, method, params] {
        if (stopped_) return;
        subscriptions_[id] = Subscription{method, params};
        openSubscription(id, subscriptions_.at(id));
    });
}

void OrcaBridgeImpl::unsubscribe(const std::string& id) {
    boost::asio::post(io_, [this, id] {
        if (stopped_) return;
        const auto it = subscriptions_.find(id);
        if (it == subscriptions_.end()) return;
        // Erase BEFORE closing the stream: closing can synchronously run a
        // frame sink that this map must no longer be under.
        const Subscription subscription = it->second;
        subscriptions_.erase(id);
        if (!connection_ || !connection_->open()) return;
        connection_->unsubscribe(id);
        // Closing the local stream does not tell Orca; transcripts would
        // otherwise accumulate server-side.
        if (subscription.method == "agentSession.subscribe" &&
            subscription.params.is_object() &&
            subscription.params.contains("sessionId") &&
            subscription.params.at("sessionId").is_string()) {
            connection_->request(newRequestId(), "agentSession.unsubscribe",
                                 nlohmann::json{{"sessionId",
                                                 subscription.params.at("sessionId")}},
                                 5'000, nullptr);
        }
    });
}

void OrcaBridgeImpl::unsubscribeAll() {
    boost::asio::post(io_, [this] {
        if (stopped_) return;
        if (connection_) {
            for (const auto& [id, subscription] : subscriptions_) {
                connection_->unsubscribe(id);
            }
        }
        subscriptions_.clear();
    });
}

}  // namespace orca::gateway
