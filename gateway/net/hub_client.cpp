#include <orca/gateway/net/hub_client.hpp>

#include <deque>
#include <random>
#include <utility>
#include <openssl/ssl.h>

namespace orca::gateway {

namespace beast = boost::beast;
namespace asio = boost::asio;
namespace websocket = beast::websocket;

namespace {

constexpr std::uint64_t kConnectTimeoutMs = 10'000;
constexpr std::uint64_t kReadSlackMs = 2'000;

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// ws:// and wss:// with an optional :port and path. The config layer
// (normalizeHubUrl) already guarantees the /gateway path for real configs.
HubClient::ParsedUrl parseUrlImpl(const std::string& url) {
    HubClient::ParsedUrl out;
    std::string rest = url;
    if (rest.rfind("wss://", 0) == 0) {
        out.secure = true;
        rest = rest.substr(6);
    } else if (rest.rfind("ws://", 0) == 0) {
        rest = rest.substr(5);
    } else {
        throw LinkError("bad_hub_url", "hub url must start with ws:// or wss://: " + url);
    }
    const std::size_t pathSlash = rest.find('/');
    const std::string hostPort =
        pathSlash == std::string::npos ? rest : rest.substr(0, pathSlash);
    out.path = pathSlash == std::string::npos ? "/" : rest.substr(pathSlash);
    const std::size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        out.host = hostPort.substr(0, colon);
        out.port = hostPort.substr(colon + 1);
    } else {
        out.host = hostPort;
        out.port = out.secure ? "443" : "80";
    }
    if (out.host.empty()) throw LinkError("bad_hub_url", "hub url has no host: " + url);
    return out;
}

}  // namespace

HubClient::HubClient(std::string hubUrl, std::string gatewayKey, std::string gatewayId,
                     std::unique_ptr<OrcaBridge> bridge,
                     std::function<void(const std::string&)> log)
    : hubUrl_(std::move(hubUrl)),
      gatewayKey_(std::move(gatewayKey)),
      gatewayId_(std::move(gatewayId)),
      bridge_(std::move(bridge)),
      log_(std::move(log)),
      workGuard_(asio::make_work_guard(io_)),
      resolver_(io_),
      core_(gatewayKey_, gatewayId_) {
    {
        std::lock_guard lock(mutex_);
        state_.hubUrl = hubUrl_;
    }
}

HubClient::~HubClient() {
    stop();
}

void HubClient::start() {
    std::lock_guard lock(mutex_);
    if (worker_.joinable() || stopped_) return;
    worker_ = std::thread([this] { threadEntry(); });
}

void HubClient::stop() {
    {
        std::lock_guard lock(mutex_);
        if (stopped_ || !worker_.joinable()) return;
        stopped_ = true;
    }
    io_.stop();
    if (worker_.joinable()) worker_.join();
}

HubClient::Snapshot HubClient::snapshot() const {
    std::lock_guard lock(mutex_);
    return state_;
}

void HubClient::threadEntry() {
    try {
        url_ = parseUrlImpl(hubUrl_);  // throws LinkError on a malformed url
    } catch (const std::exception& error) {
        std::lock_guard lock(mutex_);
        state_.lastError = error.what();
        if (log_) log_(error.what());
        return;
    }
    beginAttempt();
    io_.run();
}

void HubClient::beginAttempt() {
    {
        std::lock_guard lock(mutex_);
        state_.attempts = attempt_;
    }
    handshakeDone_ = false;
    if (url_.secure && !sslCtx_) {
        sslCtx_.emplace(asio::ssl::context::tls_client);
        // Windows keeps the system roots outside OpenSSL's default store; the
        // explicit CA bundle story lands with the P6 packaging work.
        boost::system::error_code ec;
        sslCtx_->set_default_verify_paths(ec);
        sslCtx_->set_verify_mode(asio::ssl::verify_peer);
    }
    resolver_.async_resolve(url_.host, url_.port,
                            [this](boost::system::error_code ec,
                                   asio::ip::tcp::resolver::results_type results) {
                                onResolved(ec, std::move(results));
                            });
}

void HubClient::onResolved(boost::system::error_code ec,
                            asio::ip::tcp::resolver::results_type results) {
    if (stopped_) return;
    if (ec) {
        finishAttempt("hub resolve failed: " + ec.message(), false);
        return;
    }
    // Connect a PLAIN socket first; the websocket wrappers are constructed
    // from the connected socket afterwards (the websocket stream itself has
    // no connect operation).
    auto socket = std::make_shared<asio::ip::tcp::socket>(io_);
    socket->async_connect(results,
                          [this, socket](boost::system::error_code connectEc,
                                         const asio::ip::tcp::endpoint&) {
        if (stopped_) return;
        if (connectEc) {
            finishAttempt("hub connect failed: " + connectEc.message(), false);
            return;
        }
        if (url_.secure) {
            auto tls = std::make_shared<SslWs>(std::move(*socket), *sslCtx_);
            // SNI: OpenSSL needs the hostname set on the SSL handle.
            SSL_set_tlsext_host_name(tls->next_layer().native_handle(), url_.host.c_str());
            tls->next_layer().async_handshake(
                asio::ssl::stream_base::client,
                [this, tls](boost::system::error_code handshakeEc) {
                    if (stopped_) return;
                    if (handshakeEc) {
                        finishAttempt("hub tls handshake failed: " + handshakeEc.message(),
                                      false);
                        return;
                    }
                    ws_.emplace<SslWs>(std::move(*tls));
                    onHandshake(boost::system::error_code());
                });
            return;
        }
        ws_.emplace<TcpWs>(std::move(*socket));
        onHandshake(boost::system::error_code());
    });
}

void HubClient::onHandshake(boost::system::error_code ec) {
    if (stopped_) return;
    if (ec) {
        finishAttempt("hub handshake failed: " + ec.message(), false);
        return;
    }
    handshakeDone_ = true;
    attempt_ = 0;
    {
        std::lock_guard lock(mutex_);
        state_.attempts = 0;
        state_.hubConnected = true;
    }
    if (log_) log_("hub connected: " + hubUrl_);

    const OrcaState state = bridge_->state();
    const nlohmann::json descriptor = bridge_->descriptor();
    send(core_.helloFrame(
        nlohmann::json(orcaStateName(state)), descriptor));

    lastFrameAtMs_ = nowMs();
    startPingTimer();
    doRead();
}

void HubClient::scheduleRetry() {
    const std::uint64_t delay = HubLinkCore::backoffMs(attempt_, [] {
        // Worker-thread jitter without pulling <random> state into the header.
        static std::mt19937 generator{std::random_device{}()};
        return std::uniform_real_distribution<>(0.0, 1.0)(generator);
    }());
    ++attempt_;
    if (log_) log_("hub retry in " + std::to_string(delay) + " ms");
    auto retryTimer = std::make_shared<asio::steady_timer>(io_);
    retryTimer->expires_after(std::chrono::milliseconds(delay));
    retryTimer->async_wait([this, retryTimer](boost::system::error_code ec) {
        if (stopped_ || ec) return;
        beginAttempt();
    });
}

void HubClient::finishAttempt(const std::string& why, bool resetAttempt) {
    if (resetAttempt) attempt_ = 0;
    stopPingTimer();
    {
        std::lock_guard lock(mutex_);
        state_.hubConnected = false;
        state_.lastError = why;
    }
    if (log_ && !why.empty()) log_(why);
    // The hub re-issues subscriptions after reconnecting; drop ours so nothing
    // stale streams.
    bridge_->unsubscribeAll();
    ws_.emplace<std::monostate>();
    scheduleRetry();
}

void HubClient::doRead() {
    auto onReadBound = [this](boost::system::error_code ec, std::size_t size) {
        onRead(ec, size);
    };
    if (auto* tcp = std::get_if<TcpWs>(&ws_)) {
        tcp->async_read(buffer_, onReadBound);
    } else if (auto* ssl = std::get_if<SslWs>(&ws_)) {
        ssl->async_read(buffer_, onReadBound);
    }
}

void HubClient::onRead(boost::system::error_code ec, std::size_t) {
    if (stopped_) return;
    if (ec) {
        // A timeout with an otherwise quiet link is the half-open case.
        if (ec == asio::error::operation_aborted &&
            HubLinkCore::isSilent(nowMs(), lastFrameAtMs_)) {
            teardown("hub silent for " + std::to_string(HubLinkCore::kSilenceTimeoutMs) + " ms", 4004, "hub silent");
            return;
        }
        if (ec == asio::error::operation_aborted) {
            doRead();  // not silent yet: re-arm and keep waiting
            return;
        }
        finishAttempt("hub read failed: " + ec.message(), false);
        return;
    }
    lastFrameAtMs_ = nowMs();
    onFrame();
    if (!stopped_ && handshakeDone_) doRead();
}

void HubClient::onPingTimer(boost::system::error_code ec) {
    if (stopped_ || ec || !handshakeDone_) return;
    if (HubLinkCore::isSilent(nowMs(), lastFrameAtMs_)) {
        // Half-open sockets never fire close; drop it and let the loop redial.
        teardown("hub silent (ping timer)", 4004, "hub silent");
        return;
    }
    send(nlohmann::json{{"type", "ping"}});
    startPingTimer();
}

void HubClient::startPingTimer() {
    stopPingTimer();
    pingTimer_.emplace(io_, std::chrono::milliseconds(HubLinkCore::kPingIntervalMs));
    pingTimer_->async_wait([this](boost::system::error_code ec) { onPingTimer(ec); });
}

void HubClient::stopPingTimer() {
    if (pingTimer_) {
        pingTimer_->cancel();
        pingTimer_.reset();
    }
}

void HubClient::onFrame() {
    const bool isText = std::visit([](auto& stream) -> bool {
        using T = std::decay_t<decltype(stream)>;
        if constexpr (std::is_same_v<T, TcpWs> || std::is_same_v<T, SslWs>) {
            return stream.got_text();
        }
        return false;
    }, ws_);
    if (!isText) {
        buffer_.consume(buffer_.size());
        return;
    }
    std::string payload = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    try {
        handleFrame(nlohmann::json::parse(payload));
    } catch (const nlohmann::json::exception&) {
        // malformed frames are dropped, like the TS JSON.parse try/catch
    }
}

void HubClient::handleFrame(const nlohmann::json& frame) {
    const HubLinkCore::DownDecision decision =
        HubLinkCore(gatewayKey_, gatewayId_).onHubFrame(frame);
    switch (decision.kind) {
        case HubLinkCore::DownKind::kIgnore:
            return;
        case HubLinkCore::DownKind::kLog:
            if (log_) log_(decision.message);
            return;
        case HubLinkCore::DownKind::kSendPong:
            send(nlohmann::json{{"type", "pong"}});
            return;
        case HubLinkCore::DownKind::kCloseRejected:
            if (log_) log_(decision.message);
            // The hub rejected the key: closing with 4003 ends the attempt; the
            // backoff loop keeps the process alive for a config fix.
            teardown(decision.message, 4003, "rejected");
            return;
        case HubLinkCore::DownKind::kForwardRequest:
            handleRequest(decision);
            return;
        case HubLinkCore::DownKind::kForwardSubscribe:
            bridge_->subscribe(decision.id, decision.method, decision.params);
            return;
        case HubLinkCore::DownKind::kForwardUnsubscribe:
            bridge_->unsubscribe(decision.id);
            return;
    }
}

void HubClient::handleRequest(const HubLinkCore::DownDecision& decision) {
    nlohmann::json result;
    try {
        result = bridge_->request(decision.method, decision.params, decision.timeoutMs);
        send(nlohmann::json{{"type", "request-result"},
                            {"id", decision.id},
                            {"ok", true},
                            {"result", result}});
    } catch (const LinkError& error) {
        send(nlohmann::json{{"type", "request-result"},
                            {"id", decision.id},
                            {"ok", false},
                            {"error", nlohmann::json{{"code", error.code},
                                                     {"message", error.what()}}}});
    } catch (const std::exception& error) {
        send(nlohmann::json{{"type", "request-result"},
                            {"id", decision.id},
                            {"ok", false},
                            {"error", nlohmann::json{{"code", "orca_error"},
                                                     {"message", error.what()}}}});
    }
}

void HubClient::teardown(const std::string& why, std::uint16_t closeCode, const std::string& closeReason) {
    websocket::close_reason reason;
    reason.code = closeCode;
    reason.reason = closeReason;
    if (auto* tcp = std::get_if<TcpWs>(&ws_)) {
        tcp->async_close(reason, [this, why](boost::system::error_code) { finishAttempt(why, false); });
        return;
    }
    if (auto* ssl = std::get_if<SslWs>(&ws_)) {
        ssl->async_close(reason, [this, why](boost::system::error_code) { finishAttempt(why, false); });
        return;
    }
    finishAttempt(why, false);
}

void HubClient::send(const nlohmann::json& frame) {
    if (closed()) return;
    writeQueue_.push_back(frame.dump());
    pumpWrites();
}

bool HubClient::closed() const {
    return !handshakeDone_ || std::holds_alternative<std::monostate>(ws_);
}

void HubClient::pumpWrites() {
    if (writing_ || closed() || writeQueue_.empty()) return;
    writing_ = true;
    // The buffer must outlive the operation: keep an owned copy of the frame
    // alive through the handler (the deque front can shift once the op ends).
    auto payload = std::make_shared<const std::string>(writeQueue_.front());
    auto onWrite = [this, payload](boost::system::error_code ec, std::size_t) {
        writing_ = false;
        if (!writeQueue_.empty()) writeQueue_.pop_front();
        if (ec) {
            finishAttempt("hub write failed: " + ec.message(), false);
            return;
        }
        pumpWrites();
    };
    if (auto* tcp = std::get_if<TcpWs>(&ws_)) {
        tcp->async_write(asio::buffer(*payload), onWrite);
        return;
    }
    if (auto* ssl = std::get_if<SslWs>(&ws_)) {
        ssl->async_write(asio::buffer(*payload), onWrite);
    }
}

void HubClient::handleBridgeEvent(const BridgeEvent& event) {
    // Bridge events flow through the P2 bridge's event pipe; the null bridge
    // never emits any. Kept for the P2 integration point.
    switch (event.kind) {
        case BridgeEvent::Kind::kState:
            send(nlohmann::json{{"type", "orca-state"},
                                {"state", event.state}});
            return;
        case BridgeEvent::Kind::kOrca:
            return;  // the next hello carries the descriptor
        case BridgeEvent::Kind::kStreamFrame:
            send(nlohmann::json{{"type", "stream-frame"}, {"id", event.id},
                                {"result", event.payload}});
            return;
        case BridgeEvent::Kind::kStreamError:
            send(nlohmann::json{{"type", "stream-error"}, {"id", event.id},
                                {"error", event.payload}});
            return;
        case BridgeEvent::Kind::kStreamReset:
            send(nlohmann::json{{"type", "stream-reset"}, {"id", event.id}});
            return;
    }
}

}  // namespace orca::gateway
