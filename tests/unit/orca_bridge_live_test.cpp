// P2: live loopback integration for the real Orca bridge. A minimal beast
// fixture plays "desktop Orca": it speaks the E2EE v2 handshake with its own
// keypair, validates the auth frame, replays sealed JSON-RPC and drops the
// socket on demand. The bridge under test is the REAL OrcaBridgeImpl, with
// its own worker thread, reconnect loop and subscription replay.

#include <orca/gateway/net/orca_bridge_impl.hpp>

#include <orca/crypto/base64.hpp>
#include <orca/crypto/hkdf.hpp>
#include <orca/protocol/box_crypto.hpp>
#include <orca/protocol/capabilities.hpp>
#include <orca/protocol/e2ee_handshake.hpp>
#include <orca/protocol/e2ee_session.hpp>
#include <orca/protocol/transcript.hpp>

#include "test_util.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace beast = boost::beast;
namespace asio = boost::asio;
namespace websocket = beast::websocket;
using orca::crypto::Bytes;
using nlohmann::json;

namespace {

constexpr const char* kDeviceToken = "test-device-token";

std::string hexId() {
    const auto bytes = orca::protocol::randomBytes(8);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (const auto byte : *bytes) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0xf]);
    }
    return out;
}

// Desktop Orca, reduced to the parts the bridge exercises.
class OrcaFixture : public std::enable_shared_from_this<OrcaFixture> {
public:
    OrcaFixture(asio::io_context& io, std::uint16_t port)
        : acceptor_(io, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port)),
          hostKeypair_(*orca::protocol::boxKeypair()) {}

    void run() { doAccept(); }

    void stopAccepting() {
        boost::system::error_code ec;
        acceptor_.close(ec);
    }

    const asio::ip::tcp::acceptor& acceptor() const { return acceptor_; }

    std::string hostPublicKeyB64() const {
        return orca::crypto::base64Encode(hostKeypair_.publicKey);
    }

    // --- observations ------------------------------------------------------
    int connectionCount() const { return connectionCount_; }
    bool authOk() const { return authOk_; }
    bool capabilitiesOk() const { return capabilitiesOk_; }
    bool sawStatusGet() const { return sawStatusGet_; }
    bool sawAgentSubscribe() const { return sawAgentSubscribe_; }
    bool sawAgentUnsubscribe() const { return sawAgentUnsubscribe_; }
    bool sawPingRequest() const { return sawPingRequest_; }
    std::string lastMethod() const { return lastMethod_; }
    std::string lastErrorText() const { return lastErrorText_; }

    // --- triggers ----------------------------------------------------------
    void resetAgentSubscribe() { sawAgentSubscribe_ = false; }

    void closeClient() {
        if (auto client = current_.lock()) client->close();
    }

    void sendStreamFrame(const std::string& id, const json& frame) {
        if (auto client = current_.lock()) client->sendRpc(id, true, frame);
    }

    void sendStreamError(const std::string& id) {
        if (auto client = current_.lock()) {
            client->sendRpc(id, false, json(), json{{"code", "stream_broke"},
                                                    {"message", "stream broke"}});
        }
    }

private:
    class Client : public std::enable_shared_from_this<Client> {
    public:
        Client(OrcaFixture& fixture, asio::ip::tcp::socket socket)
            : fixture_(fixture), ws_(std::move(socket)) {}

        void start() {
            ws_.text(true);
            ws_.async_accept(beast::bind_front_handler(&Client::onAccept, shared_from_this()));
        }

        void close() {
            if (closing_) return;
            closing_ = true;
            auto self = shared_from_this();
            websocket::close_reason reason;
            reason.code = 1011;
            reason.reason = "fixture drop";
            ws_.async_close(reason, [self](boost::system::error_code) {});
        }

        void sendText(std::string text) {
            out_.push_back(std::move(text));
            pump();
        }

        // Seal any JSON body as one desktop-to-mobile text frame. The fixture is
        // the desktop end, so it cannot reuse E2eeSession (that is the mobile
        // half: it seals mobile-to-desktop and opens desktop-to-mobile).
        void sendSealed(const json& body) {
            if (!schedule_) return;
            const std::string text = body.dump();
            const auto plaintext = Bytes(text.begin(), text.end());
            const auto frame = orca::protocol::sealFrameAt(*schedule_, orca::protocol::Direction::kDesktopToMobile,
                                                           orca::protocol::PayloadKind::kText,
                                                           outboundCounter_++, plaintext);
            if (frame) sendText(orca::crypto::base64Encode(*frame));
        }

        // A sealed RPC envelope for `id`.
        void sendRpc(const std::string& id, bool ok, const json& result,
                     const json& error = json()) {
            json envelope{{"id", id}, {"ok", ok}};
            if (ok) {
                envelope["result"] = result;
            } else {
                envelope["error"] = error;
            }
            sendSealed(envelope);
        }

    private:
        // The client seals mobile-to-desktop; the fixture opens that direction.
        std::optional<std::string> openSealed(const std::string& text) {
            if (!schedule_) return std::nullopt;
            Bytes frame;
            if (!orca::crypto::base64Decode(text, frame)) return std::nullopt;
            const auto payload =
                orca::protocol::openFrameAt(*schedule_, orca::protocol::Direction::kMobileToDesktop,
                                            orca::protocol::PayloadKind::kText,
                                            inboundCounter_++, frame);
            if (!payload) return std::nullopt;
            return std::string(payload->begin(), payload->end());
        }

        void onAccept(boost::system::error_code ec) {
            if (ec) return;
            ++fixture_.connectionCount_;
            read();
        }

        void pump() {
            if (writing_ || out_.empty()) return;
            writing_ = true;
            auto self = shared_from_this();
            ws_.async_write(asio::buffer(out_.front()),
                            [this, self](boost::system::error_code ec, std::size_t) {
                                writing_ = false;
                                if (!out_.empty()) out_.pop_front();
                                if (!ec) pump();
                            });
        }

        void read() {
            auto self = shared_from_this();
            ws_.async_read(buffer_, [this, self](boost::system::error_code ec, std::size_t) {
                if (ec) return;
                const std::string payload = beast::buffers_to_string(buffer_.data());
                buffer_.consume(buffer_.size());
                handle(payload);
                if (!closing_) read();
            });
        }

        void handle(const std::string& text) {
            switch (stage_) {
                case Stage::kHello:
                    return onHello(text);
                case Stage::kAuth:
                    return onAuth(text);
                case Stage::kReady:
                    return onRpc(text);
            }
        }

        void onHello(const std::string& text) {
            json hello;
            try {
                hello = json::parse(text);
            } catch (const json::exception&) {
                return fixture_.noteError("hello was not JSON");
            }
            if (hello.value("type", "") != "e2ee_hello") {
                return fixture_.noteError("expected e2ee_hello");
            }
            Bytes publicKey;
            Bytes nonce;
            if (!orca::crypto::base64Decode(hello.value("clientPublicKeyB64", ""), publicKey) ||
                !orca::crypto::base64Decode(hello.value("clientNonceB64", ""), nonce) ||
                publicKey.size() != 32 || nonce.size() != 32) {
                return fixture_.noteError("hello carried a malformed key/nonce");
            }
            clientPublicKey_ = publicKey;
            clientNonce_ = nonce;
            hostNonce_ = *orca::protocol::randomBytes(32);

            sendText(json{{"type", "e2ee_ready"},
                          {"v", 2},
                          {"clientNonceB64", hello.at("clientNonceB64")},
                          {"selection",
                           json{{"framing", 2},
                                {"payloadKinds", json::array({"text", "binary"})}}},
                          {"context", hello.at("context")},
                          {"desktopPublicKeyB64", fixture_.hostPublicKeyB64()},
                          {"desktopNonceB64", orca::crypto::base64Encode(hostNonce_)}}
                         .dump());
            stage_ = Stage::kAuth;
        }

        void onAuth(const std::string& text) {
            if (!schedule_) {
                const auto shared = orca::protocol::boxBeforenm(clientPublicKey_,
                                                                fixture_.hostKeypair_.secretKey);
                if (!shared) return fixture_.noteError("beforenm failed");
                orca::protocol::TranscriptInput input;
                input.clientPublicKey = clientPublicKey_;
                input.clientNonce = clientNonce_;
                input.hostPublicKey = fixture_.hostKeypair_.publicKey;
                input.hostNonce = hostNonce_;
                input.transport = "direct";
                const auto transcript = orca::protocol::encodeTranscript(input);
                schedule_ = orca::crypto::deriveE2eeSchedule(*shared, clientNonce_, hostNonce_,
                                                             transcript);
                transcriptHash_ = orca::protocol::transcriptHashB64(schedule_->transcriptHash);
            }
            const auto plaintext = openSealed(text);
            if (!plaintext) {
                return fixture_.noteError("auth frame did not decrypt (" +
                                          std::to_string(text.size()) + " chars)");
            }
            json auth;
            try {
                auth = json::parse(*plaintext);
            } catch (const json::exception&) {
                return fixture_.noteError("auth frame was not JSON");
            }
            if (!orca::protocol::isE2eeAuthenticated(
                    json{{"type", "e2ee_authenticated"},
                         {"v", 2},
                         {"transcriptHashB64", auth.value("transcriptHashB64", "")}},
                    transcriptHash_) ||
                auth.value("deviceToken", "") != kDeviceToken) {
                return fixture_.noteError("auth rejected: token or transcript hash mismatch");
            }
            fixture_.authOk_ = true;
            stage_ = Stage::kReady;
            // The auth reply is a bare control frame, not an RPC envelope.
            sendSealed(json{{"type", "e2ee_authenticated"},
                            {"v", 2},
                            {"transcriptHashB64", transcriptHash_}});
        }

        void onRpc(const std::string& text) {
            const auto plaintext = openSealed(text);
            if (!plaintext) return;
            json request;
            try {
                request = json::parse(*plaintext);
            } catch (const json::exception&) {
                return;
            }
            const std::string id = request.value("id", "");
            const std::string method = request.value("method", "");
            fixture_.lastMethod_ = method;
            if (request.value("deviceToken", "") != kDeviceToken) {
                return fixture_.noteError("rpc carried the wrong device token");
            }
            if (method == orca::protocol::kClientCapabilitiesMethod) {
                const auto& caps = request.at("params").at("clientCapabilities");
                fixture_.capabilitiesOk_ =
                    caps.is_array() && caps.size() == orca::protocol::kClientCapabilityCount;
                if (!fixture_.capabilitiesOk_) fixture_.noteError("capabilities were not replayed");
                return sendRpc(id, true, json{{"accepted", true}});
            }
            if (method == "status.get") {
                fixture_.sawStatusGet_ = true;
                return sendRpc(id, true, json{{"appVersion", "9.9.9"},
                                              {"runtimeProtocolVersion", 6},
                                              {"hostPlatform", "win32"},
                                              {"machineName", "FIXTURE-PC"}});
            }
            if (method == "agentSession.subscribe") {
                fixture_.sawAgentSubscribe_ = true;
                agentSubscribeId_ = id;
                return sendRpc(id, true, json{{"type", "snapshot"}, {"messages", 0}});
            }
            if (method == "agentSession.unsubscribe") {
                fixture_.sawAgentUnsubscribe_ = true;
                return sendRpc(id, true, json{{"ok", true}});
            }
            if (method == "orca.ping") {
                fixture_.sawPingRequest_ = true;
                return sendRpc(id, true, json::array({"pong"}));
            }
            sendRpc(id, false, json(), json{{"code", "not_found"}, {"message", "no such method"}});
        }

        enum class Stage { kHello, kAuth, kReady };

        OrcaFixture& fixture_;
        websocket::stream<asio::ip::tcp::socket> ws_;
        beast::flat_buffer buffer_;
        std::deque<std::string> out_;
        bool writing_ = false;
        bool closing_ = false;
        Stage stage_ = Stage::kHello;

        Bytes clientPublicKey_;
        Bytes clientNonce_;
        Bytes hostNonce_;
        std::optional<orca::crypto::E2eeKeySchedule> schedule_;
        std::string transcriptHash_;
        std::uint64_t inboundCounter_ = 0;
        std::uint64_t outboundCounter_ = 0;
        std::string agentSubscribeId_;
    };

    void noteError(const std::string& text) {
        lastErrorText_ = text;
        std::fprintf(stderr, "[fixture] %s\n", text.c_str());
    }

    void doAccept() {
        auto socket = std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
        acceptor_.async_accept(*socket, [this, socket](boost::system::error_code ec) {
            if (ec) return;
            auto client = std::make_shared<Client>(*this, std::move(*socket));
            current_ = client;
            client->start();
            doAccept();
        });
    }

    asio::ip::tcp::acceptor acceptor_;
    orca::protocol::BoxKeyPair hostKeypair_;
    std::weak_ptr<Client> current_;
    int connectionCount_ = 0;
    std::atomic<bool> authOk_{false};
    std::atomic<bool> capabilitiesOk_{false};
    std::atomic<bool> sawStatusGet_{false};
    std::atomic<bool> sawAgentSubscribe_{false};
    std::atomic<bool> sawAgentUnsubscribe_{false};
    std::atomic<bool> sawPingRequest_{false};
    std::string lastMethod_;
    std::string lastErrorText_;
};

template <typename Pred>
bool waitFor(Pred predicate, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return predicate();
}

// One completion for one bridge request, observed from the test thread.
struct Outcome {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
    json result;
    std::string code;
    std::string message;

    orca::gateway::OrcaBridge::RequestDone doneFn() {
        return [this](std::optional<json> value, std::optional<orca::gateway::LinkError> error) {
            std::lock_guard lock(mutex);
            done = true;
            if (error) {
                code = error->code;
                message = error->what();
            } else {
                ok = true;
                result = value ? *value : json();
            }
            cv.notify_all();
        };
    }

    bool wait(int timeoutMs) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return done; });
    }
};

}  // namespace

int main() {
    // Unbuffered: if a step throws, the ok/FAIL lines leading up to it survive.
    std::cout << std::unitbuf;
    asio::io_context io;
    auto work = asio::make_work_guard(io);
    auto fixture = std::make_shared<OrcaFixture>(io, 0);
    fixture->run();
    std::thread ioThread([&] { io.run(); });
    const std::uint16_t port = fixture->acceptor().local_endpoint().port();

    orca::gateway::OrcaBridgeOptions options;
    options.endpoint = "ws://127.0.0.1:" + std::to_string(port);
    options.deviceToken = kDeviceToken;
    options.hostPublicKeyB64 = fixture->hostPublicKeyB64();
    auto bridge = std::make_unique<orca::gateway::OrcaBridgeImpl>(
        options, [](const std::string& message) { std::fprintf(stderr, "[bridge] %s\n", message.c_str()); });
    auto* bridgeRaw = bridge.get();

    std::mutex eventMutex;
    std::vector<std::pair<std::string, std::string>> events;  // (state, id/payload marker)
    bridge->setEventSink([&](const orca::gateway::BridgeEvent& event) {
        std::lock_guard lock(eventMutex);
        switch (event.kind) {
            case orca::gateway::BridgeEvent::Kind::kState:
                events.emplace_back("state:" + event.state, event.message);
                break;
            case orca::gateway::BridgeEvent::Kind::kOrca:
                events.emplace_back("orca", event.payload.dump());
                break;
            case orca::gateway::BridgeEvent::Kind::kStreamFrame:
                events.emplace_back("frame:" + event.id, event.payload.dump());
                break;
            case orca::gateway::BridgeEvent::Kind::kStreamError:
                events.emplace_back("stream-error:" + event.id, event.payload.dump());
                break;
            case orca::gateway::BridgeEvent::Kind::kStreamReset:
                events.emplace_back("reset:" + event.id, "");
                break;
        }
    });
    auto countEvents = [&](const std::string& prefix) {
        std::lock_guard lock(eventMutex);
        int count = 0;
        for (const auto& [kind, payload] : events) {
            if (kind.rfind(prefix, 0) == 0) ++count;
        }
        return count;
    };

    bridge->start();

    // 1. E2EE handshake + per-socket capabilities.
    test::check(waitFor([&] { return bridgeRaw->state() == orca::gateway::OrcaState::kConnected; },
                        10000),
                "the bridge reaches connected after the E2EE handshake");
    test::check(waitFor([&] { return fixture->capabilitiesOk(); }, 5000),
                "capabilities are replayed on this socket before any caller request");
    test::check(fixture->authOk(), "the fixture accepted our sealed e2ee_auth");

    // 2. status.get populates the descriptor.
    test::check(waitFor([&] { return fixture->sawStatusGet(); }, 5000), "status.get was sent");
    test::check(waitFor([&] { return countEvents("orca") >= 1; }, 5000),
                "the descriptor event reached the sink");
    {
        std::lock_guard lock(eventMutex);
        const json descriptor = json::parse(events.back().second);
        test::check(descriptor.value("appVersion", "") == "9.9.9" &&
                        descriptor.value("protocolVersion", 0) == 6 &&
                        descriptor.value("machineName", "") == "FIXTURE-PC",
                    "the descriptor is mapped to appVersion/protocolVersion/machineName");
    }

    // 3. A sealed request round-trips.
    {
        Outcome outcome;
        bridge->request("rq-1", "orca.ping", json(), 5000, outcome.doneFn());
        test::check(outcome.wait(5000) && outcome.ok && outcome.result == json::array({"pong"}),
                    "orca.ping round-trips through the sealed socket");
    }

    // 4. An Orca error map comes back as a LinkError with its code.
    {
        Outcome outcome;
        bridge->request("rq-2", "no.such.method", json(), 5000, outcome.doneFn());
        test::check(outcome.wait(5000) && !outcome.ok && outcome.code == "not_found",
                    "an Orca error frame surfaces as a coded LinkError");
    }

    // 5. Subscriptions: first frame, then stream frames and a stream error.
    bridge->subscribe("sub-1", "agentSession.subscribe", json{{"sessionId", "s-1"}});
    test::check(waitFor([&] { return fixture->sawAgentSubscribe(); }, 5000),
                "agentSession.subscribe reached Orca");
    test::check(waitFor([&] { return countEvents("frame:sub-1") >= 1; }, 5000),
                "the subscription's first result reaches the sink as a stream frame");
    fixture->sendStreamFrame("sub-1", json{{"type", "delta"}, {"text", "hi"}});
    test::check(waitFor([&] { return countEvents("frame:sub-1") >= 2; }, 5000),
                "a streamed part reaches the sink");
    fixture->sendStreamError("sub-1");
    test::check(waitFor([&] { return countEvents("stream-error:sub-1") >= 1; }, 5000),
                "an Orca stream error becomes a stream-error event with the Orca code");

    // 6. unsubscribe tells Orca (transcripts must not pile up server-side).
    //    The unsubscribed stream must go quiet: nothing may follow it.
    bridge->unsubscribe("sub-1");
    test::check(waitFor([&] { return fixture->sawAgentUnsubscribe(); }, 5000),
                "unsubscribe sends agentSession.unsubscribe to Orca");
    const int framesBefore = countEvents("frame:sub-1");
    fixture->sendStreamFrame("sub-1", json{{"type", "delta"}, {"text", "late"}});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    test::check(countEvents("frame:sub-1") == framesBefore,
                "a frame for an unsubscribed stream is dropped");

    // 7. A drop flips the state and the bridge redials, replaying subscriptions.
    fixture->resetAgentSubscribe();
    bridge->subscribe("sub-2", "agentSession.subscribe", json{{"sessionId", "s-2"}});
    test::check(waitFor([&] { return fixture->sawAgentSubscribe(); }, 5000),
                "the second subscription opened on the live socket");
    fixture->closeClient();
    test::check(waitFor([&] { return countEvents("state:disconnected") >= 1; }, 5000),
                "a dropped socket reports disconnected");
    test::check(waitFor([&] { return fixture->connectionCount() >= 2; }, 10000),
                "the bridge redials Orca");
    test::check(waitFor([&] { return countEvents("reset:sub-2") >= 1; }, 10000),
                "subscriptions are replayed with a stream-reset first");
    test::check(waitFor([&] { return bridgeRaw->state() == orca::gateway::OrcaState::kConnected; },
                        10000),
                "the redialed socket reaches connected again");

    // 8. Shutdown joins the worker without hanging.
    bridge->shutdown();

    bridge.reset();
    fixture->stopAccepting();
    work.reset();
    ioThread.join();
    if (!fixture->lastErrorText().empty()) {
        std::fprintf(stderr, "[fixture] last error: %s\n", fixture->lastErrorText().c_str());
    }
    return test::finish("orca_bridge_live_test");
}
