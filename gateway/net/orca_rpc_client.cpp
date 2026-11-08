#include <orca/gateway/net/orca_rpc_client.hpp>

#include <orca/gateway/net/tls_roots.hpp>
#include <orca/crypto/base64.hpp>
#include <orca/crypto/hkdf.hpp>
#include <orca/protocol/box_crypto.hpp>
#include <orca/protocol/capabilities.hpp>
#include <orca/protocol/e2ee_handshake.hpp>
#include <orca/protocol/e2ee_session.hpp>
#include <orca/protocol/transcript.hpp>

#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <utility>
#include <variant>

namespace orca::gateway {

namespace beast = boost::beast;
namespace asio = boost::asio;
namespace websocket = beast::websocket;

namespace {

struct ParsedUrl {
    bool secure = false;
    std::string host;
    std::string port;
    std::string path;
};

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl out;
    std::string rest = url;
    if (rest.rfind("wss://", 0) == 0) {
        out.secure = true;
        rest = rest.substr(6);
    } else if (rest.rfind("ws://", 0) == 0) {
        rest = rest.substr(5);
    } else {
        throw LinkError("remote_runtime_unavailable",
                        "Orca endpoint must start with ws:// or wss://: " + url);
    }
    const std::size_t slash = rest.find('/');
    const std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    const std::size_t colon = authority.rfind(':');
    if (colon == std::string::npos) {
        out.host = authority;
        out.port = out.secure ? "443" : "80";
    } else {
        out.host = authority.substr(0, colon);
        out.port = authority.substr(colon + 1);
    }
    if (out.host.empty()) {
        throw LinkError("remote_runtime_unavailable", "Orca endpoint has no host: " + url);
    }
    return out;
}

std::string requestId() {
    const auto bytes = orca::protocol::randomBytes(16);
    if (!bytes) return "orca-request";
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes->size() * 2);
    for (const auto byte : *bytes) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0xf]);
    }
    return out;
}

LinkError unavailable(const std::string& message) {
    return LinkError("remote_runtime_unavailable", message);
}

}  // namespace

struct OrcaConnection::Impl : std::enable_shared_from_this<OrcaConnection::Impl> {
    using TcpWs = websocket::stream<asio::ip::tcp::socket>;
    using SslWs = websocket::stream<asio::ssl::stream<asio::ip::tcp::socket>>;
    using Ws = std::variant<std::monostate, TcpWs, SslWs>;

    enum class Stage { kConnecting, kAwaitReady, kAwaitAuth, kReady, kClosed };

    Impl(asio::io_context& io, Options options, CloseSink onClose, ConnectDone done)
        : io_(io),
          options_(std::move(options)),
          onClose_(std::move(onClose)),
          connectDone_(std::move(done)),
          resolver_(io) {}

    asio::io_context& io_;
    Options options_;
    CloseSink onClose_;
    ConnectDone connectDone_;
    ParsedUrl url_;
    asio::ip::tcp::resolver resolver_;
    std::optional<asio::ssl::context> sslCtx_;
    Ws ws_;
    beast::flat_buffer buffer_;
    std::deque<std::string> writeQueue_;
    bool writing_ = false;
    std::optional<asio::steady_timer> handshakeTimer_;

    Stage stage_ = Stage::kConnecting;
    bool connectDoneFired_ = false;
    bool closeNotified_ = false;

    nlohmann::json hello_;
    orca::crypto::Bytes clientPublicKey_;
    orca::crypto::Bytes clientSecretKey_;
    orca::crypto::Bytes clientNonce_;
    orca::crypto::Bytes pinnedHostKey_;
    std::optional<orca::protocol::E2eeSession> session_;
    std::string transcriptHash_;

    struct PendingRequest {
        RequestDone done;
        std::shared_ptr<asio::steady_timer> timer;
    };
    std::map<std::string, PendingRequest> pending_;
    std::map<std::string, FrameSink> subscriptions_;

    // --- lifecycle ---------------------------------------------------------

    void start() {
        try {
            url_ = parseUrl(options_.endpoint);
        } catch (const LinkError& error) {
            fail(error);
            return;
        }
        if (!orca::crypto::base64Decode(options_.hostPublicKeyB64, pinnedHostKey_) ||
            pinnedHostKey_.size() != 32) {
            fail(unavailable("Invalid Orca host public key"));
            return;
        }
        const auto keypair = orca::protocol::boxKeypair();
        const auto nonce = orca::protocol::randomBytes(32);
        if (!keypair || !nonce) {
            fail(unavailable("Could not generate the E2EE client key"));
            return;
        }
        clientPublicKey_ = keypair->publicKey;
        clientSecretKey_ = keypair->secretKey;
        clientNonce_ = *nonce;

        orca::protocol::E2eeContext context;
        if (!options_.transport.empty()) context.transport = options_.transport;
        if (!options_.relayHostId.empty()) context.relayHostId = options_.relayHostId;
        const auto hello = orca::protocol::makeE2eeHello(keypair->publicKey, clientNonce_, context);
        if (!hello) {
            fail(unavailable("Could not build the E2EE hello"));
            return;
        }
        hello_ = *hello;

        handshakeTimer_.emplace(io_);
        handshakeTimer_->expires_after(std::chrono::milliseconds(options_.handshakeTimeoutMs));
        auto self = shared_from_this();
        handshakeTimer_->async_wait([self](boost::system::error_code ec) {
            if (ec || self->stage_ == Stage::kReady || self->stage_ == Stage::kClosed) return;
            self->fail(unavailable(std::string("Handshake timed out at stage ") +
                                   self->stageName()));
        });

        resolver_.async_resolve(url_.host, url_.port,
                                [self](boost::system::error_code ec,
                                       asio::ip::tcp::resolver::results_type results) {
                                    self->onResolved(ec, std::move(results));
                                });
    }

    const char* stageName() const {
        switch (stage_) {
            case Stage::kConnecting: return "connect";
            case Stage::kAwaitReady: return "handshake";
            case Stage::kAwaitAuth: return "auth";
            case Stage::kReady: return "socket";
            case Stage::kClosed: return "closed";
        }
        return "connect";
    }

    void onResolved(boost::system::error_code ec,
                    asio::ip::tcp::resolver::results_type results) {
        if (stage_ == Stage::kClosed) return;
        if (ec) {
            fail(unavailable("Could not resolve Orca endpoint: " + ec.message()));
            return;
        }
        auto socket = std::make_shared<asio::ip::tcp::socket>(io_);
        auto self = shared_from_this();
        asio::async_connect(*socket, results,
                            [self, socket](boost::system::error_code connectEc,
                                           const asio::ip::tcp::endpoint&) {
                                self->onTcpConnected(connectEc, std::move(*socket));
                            });
    }

    void onTcpConnected(boost::system::error_code ec, asio::ip::tcp::socket socket) {
        if (stage_ == Stage::kClosed) return;
        if (ec) {
            fail(unavailable("Could not reach Orca: " + ec.message()));
            return;
        }
        auto self = shared_from_this();
        if (url_.secure) {
            if (!sslCtx_) {
                sslCtx_.emplace(asio::ssl::context::tls_client);
                installSystemTrust(*sslCtx_);
                sslCtx_->set_verify_mode(asio::ssl::verify_peer);
            }
            auto tls = std::make_shared<SslWs>(std::move(socket), *sslCtx_);
            SSL_set_tlsext_host_name(tls->next_layer().native_handle(), url_.host.c_str());
            tls->next_layer().async_handshake(
                asio::ssl::stream_base::client,
                [self, tls](boost::system::error_code handshakeEc) {
                    if (self->stage_ == Stage::kClosed) return;
                    if (handshakeEc) {
                        self->fail(unavailable("Orca TLS handshake failed: " +
                                               handshakeEc.message()));
                        return;
                    }
                    self->ws_.emplace<SslWs>(std::move(*tls));
                    self->startWsHandshake();
                });
            return;
        }
        ws_.emplace<TcpWs>(std::move(socket));
        startWsHandshake();
    }

    void startWsHandshake() {
        const std::string defaultPort = url_.secure ? "443" : "80";
        const std::string host =
            url_.port == defaultPort ? url_.host : url_.host + ":" + url_.port;
        auto self = shared_from_this();
        auto onUpgrade = [self](boost::system::error_code ec) { self->onWsHandshake(ec); };
        if (auto* tcp = std::get_if<TcpWs>(&ws_)) {
            tcp->text(true);
            tcp->async_handshake(host, url_.path, onUpgrade);
            return;
        }
        if (auto* ssl = std::get_if<SslWs>(&ws_)) {
            ssl->text(true);
            ssl->async_handshake(host, url_.path, onUpgrade);
        }
    }

    void onWsHandshake(boost::system::error_code ec) {
        if (stage_ == Stage::kClosed) return;
        if (ec) {
            fail(unavailable("Orca websocket handshake failed: " + ec.message()));
            return;
        }
        stage_ = Stage::kAwaitReady;
        sendPlain(hello_.dump());
        doRead();
    }

    // --- handshake frames --------------------------------------------------

    void onText(const std::string& text) {
        switch (stage_) {
            case Stage::kAwaitReady: onReady(text); return;
            case Stage::kAwaitAuth: onAuthReply(text); return;
            case Stage::kReady: onSealedFrame(text); return;
            case Stage::kConnecting:
            case Stage::kClosed: return;
        }
    }

    void onReady(const std::string& text) {
        nlohmann::json ready;
        try {
            ready = nlohmann::json::parse(text);
        } catch (const nlohmann::json::exception&) {
            fail(unavailable("Orca sent a non-JSON e2ee_ready"));
            return;
        }
        const auto accepted = orca::protocol::acceptE2eeReady(ready, hello_, pinnedHostKey_);
        if (!accepted) {
            fail(unavailable("Orca returned an e2ee_ready this client does not accept "
                             "(host key or transcript mismatch)"));
            return;
        }
        const auto shared = orca::protocol::boxBeforenm(accepted->hostPublicKey, clientSecretKey_);
        if (!shared) {
            fail(unavailable("Could not derive the E2EE shared secret"));
            return;
        }
        orca::protocol::TranscriptInput input;
        input.clientPublicKey = clientPublicKey_;
        input.clientNonce = clientNonce_;
        input.hostPublicKey = accepted->hostPublicKey;
        input.hostNonce = accepted->hostNonce;
        input.transport = options_.transport.empty() ? "direct" : options_.transport;
        input.relayHostId = options_.relayHostId;
        const auto transcript = orca::protocol::encodeTranscript(input);
        const auto schedule = orca::crypto::deriveE2eeSchedule(*shared, clientNonce_,
                                                               accepted->hostNonce, transcript);
        auto session = orca::protocol::E2eeSession::create(schedule);
        if (!session) {
            fail(unavailable("Could not create the E2EE session"));
            return;
        }
        session_.emplace(std::move(*session));
        transcriptHash_ = orca::protocol::transcriptHashB64(schedule.transcriptHash);
        stage_ = Stage::kAwaitAuth;
        sendSealed(orca::protocol::makeE2eeAuth(transcriptHash_, options_.deviceToken));
    }

    void onAuthReply(const std::string& text) {
        const auto plaintext = session_ ? session_->openText(text) : std::nullopt;
        if (!plaintext) {
            fail(unavailable("Orca auth reply could not be decrypted"));
            return;
        }
        nlohmann::json reply;
        try {
            reply = nlohmann::json::parse(*plaintext);
        } catch (const nlohmann::json::exception&) {
            fail(unavailable("Orca auth reply was not JSON"));
            return;
        }
        if (!orca::protocol::isE2eeAuthenticated(reply, transcriptHash_)) {
            fail(unavailable("Orca rejected the device token"));
            return;
        }
        stage_ = Stage::kReady;
        if (handshakeTimer_) {
            handshakeTimer_->cancel();
            handshakeTimer_.reset();
        }
        // Capabilities are per socket: replay them before any caller request.
        auto self = shared_from_this();
        const std::string id = requestId();
        request(id, orca::protocol::kClientCapabilitiesMethod,
                orca::protocol::makeClientCapabilitiesParams(), options_.requestTimeoutMs,
                [self](std::optional<nlohmann::json>, std::optional<LinkError> error) {
                    if (error) {
                        self->fail(*error);
                        return;
                    }
                    self->completeConnect();
                });
    }

    void completeConnect() {
        if (connectDoneFired_) return;
        connectDoneFired_ = true;
        auto done = std::move(connectDone_);
        if (done) done(Ptr(new OrcaConnection(shared_from_this())), std::nullopt);
    }

    // --- sealed RPC --------------------------------------------------------

    void onSealedFrame(const std::string& text) {
        const auto plaintext = session_ ? session_->openText(text) : std::nullopt;
        if (!plaintext) {
            fail(unavailable("Orca sent an undecryptable frame"));
            return;
        }
        nlohmann::json frame;
        try {
            frame = nlohmann::json::parse(*plaintext);
        } catch (const nlohmann::json::exception&) {
            fail(unavailable("Orca sent a non-JSON frame"));
            return;
        }
        dispatch(frame);
    }

    void dispatch(const nlohmann::json& frame) {
        if (!frame.is_object()) return;
        if (frame.contains("_keepalive") && frame.at("_keepalive") == true) return;
        if (!frame.contains("id") || !frame.at("id").is_string()) return;
        const std::string id = frame.at("id").get<std::string>();

        const auto pending = pending_.find(id);
        if (pending != pending_.end()) {
            PendingRequest request = std::move(pending->second);
            pending_.erase(pending);
            if (request.timer) request.timer->cancel();
            // Fire-and-forget requests (agentSession.unsubscribe) pass no
            // completion, so never call an empty std::function.
            if (!request.done) return;
            if (frame.value("ok", false)) {
                nlohmann::json result =
                    frame.contains("result") ? frame.at("result") : nlohmann::json();
                request.done(std::optional<nlohmann::json>(std::move(result)), std::nullopt);
            } else {
                request.done(std::nullopt, errorFromFrame(frame));
            }
            return;
        }
        const auto subscription = subscriptions_.find(id);
        if (subscription != subscriptions_.end()) {
            // Copy the sink first and hand it a detached payload: a sink may
            // call back into this connection, and it must not see this map
            // change underneath the iterator.
            FrameSink sink = subscription->second;
            nlohmann::json payload;
            // Mirror the TS subscribe(): a successful part is delivered as its
            // `result`, a failed one as {type:'error', error}. The bridge keys
            // off `type` to tell stream frames from stream errors.
            if (frame.value("ok", false)) {
                payload = frame.contains("result") ? frame.at("result") : nlohmann::json();
            } else {
                const LinkError error = errorFromFrame(frame);
                payload = nlohmann::json{{"type", "error"},
                                         {"error", nlohmann::json{{"code", error.code},
                                                                  {"message", error.what()}}}};
            }
            if (sink) sink(payload);
        }
    }

    static LinkError errorFromFrame(const nlohmann::json& frame) {
        std::string code = "remote_runtime_unavailable";
        std::string message = "Orca request failed";
        if (frame.contains("error") && frame.at("error").is_object()) {
            const auto& error = frame.at("error");
            if (error.contains("code") && error.at("code").is_string()) {
                code = error.at("code").get<std::string>();
            }
            if (error.contains("message") && error.at("message").is_string()) {
                message = error.at("message").get<std::string>();
            }
        }
        return LinkError(std::move(code), message);
    }

    // --- writes ------------------------------------------------------------

    void sendPlain(const std::string& text) { enqueue(text); }

    void sendSealed(const nlohmann::json& frame) {
        if (!session_) {
            fail(unavailable("E2EE session is not ready"));
            return;
        }
        const auto sealed = session_->sealText(frame.dump());
        if (!sealed) {
            fail(unavailable("Could not seal the Orca frame"));
            return;
        }
        enqueue(*sealed);
    }

    void enqueue(std::string payload) {
        if (stage_ == Stage::kClosed) return;
        writeQueue_.push_back(std::move(payload));
        pumpWrites();
    }

    void pumpWrites() {
        if (writing_ || writeQueue_.empty() || stage_ == Stage::kClosed) return;
        writing_ = true;
        auto payload = std::make_shared<const std::string>(writeQueue_.front());
        auto self = shared_from_this();
        auto onWrite = [self, payload](boost::system::error_code ec, std::size_t) {
            self->writing_ = false;
            if (!self->writeQueue_.empty()) self->writeQueue_.pop_front();
            if (self->stage_ == Stage::kClosed) return;
            if (ec) {
                self->fail(unavailable("Orca socket write failed: " + ec.message()));
                return;
            }
            self->pumpWrites();
        };
        if (auto* tcp = std::get_if<TcpWs>(&ws_)) {
            tcp->async_write(asio::buffer(*payload), onWrite);
            return;
        }
        if (auto* ssl = std::get_if<SslWs>(&ws_)) {
            ssl->async_write(asio::buffer(*payload), onWrite);
            return;
        }
        writing_ = false;
    }

    // --- reads -------------------------------------------------------------

    void doRead() {
        if (stage_ == Stage::kClosed) return;
        auto self = shared_from_this();
        auto onRead = [self](boost::system::error_code ec, std::size_t) { self->onRead(ec); };
        if (auto* tcp = std::get_if<TcpWs>(&ws_)) {
            tcp->async_read(buffer_, onRead);
            return;
        }
        if (auto* ssl = std::get_if<SslWs>(&ws_)) {
            ssl->async_read(buffer_, onRead);
        }
    }

    void onRead(boost::system::error_code ec) {
        if (stage_ == Stage::kClosed) return;
        if (ec) {
            fail(unavailable("Orca socket read failed: " + ec.message()));
            return;
        }
        const bool text = std::visit([](auto& stream) -> bool {
            using T = std::decay_t<decltype(stream)>;
            if constexpr (std::is_same_v<T, TcpWs> || std::is_same_v<T, SslWs>) {
                return stream.got_text();
            } else {
                return false;
            }
        }, ws_);
        if (!text) {
            buffer_.consume(buffer_.size());
            fail(unavailable("Orca sent a binary frame"));
            return;
        }
        const std::string payload = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());
        onText(payload);
        if (stage_ != Stage::kClosed) doRead();
    }

    // --- teardown ----------------------------------------------------------

    void failPending(const LinkError& error) {
        auto pending = std::move(pending_);
        pending_.clear();
        for (auto& [id, request] : pending) {
            if (request.timer) request.timer->cancel();
            if (request.done) request.done(std::nullopt, error);
        }
    }

    void notifyClose(std::optional<LinkError> error) {
        if (closeNotified_) return;
        closeNotified_ = true;
        auto sink = std::move(onClose_);
        if (sink) sink(std::move(error));
    }

    void closeSocket() {
        boost::system::error_code ignored;
        if (auto* tcp = std::get_if<TcpWs>(&ws_)) {
            beast::get_lowest_layer(*tcp).close(ignored);
        } else if (auto* ssl = std::get_if<SslWs>(&ws_)) {
            beast::get_lowest_layer(*ssl).close(ignored);
        }
    }

    void fail(LinkError error) {
        if (stage_ == Stage::kClosed) return;
        stage_ = Stage::kClosed;
        if (handshakeTimer_) {
            handshakeTimer_->cancel();
            handshakeTimer_.reset();
        }
        failPending(error);
        subscriptions_.clear();
        closeSocket();
        if (!connectDoneFired_) {
            connectDoneFired_ = true;
            auto done = std::move(connectDone_);
            if (done) done(nullptr, error);
            return;
        }
        notifyClose(std::move(error));
    }

    void closeGracefully(const std::string& reason) {
        if (stage_ == Stage::kClosed) return;
        stage_ = Stage::kClosed;
        if (handshakeTimer_) {
            handshakeTimer_->cancel();
            handshakeTimer_.reset();
        }
        failPending(unavailable("Orca connection closed"));
        subscriptions_.clear();
        websocket::close_reason closeReason;
        closeReason.code = 1000;
        closeReason.reason = reason;
        auto self = shared_from_this();
        auto onClosed = [self](boost::system::error_code) {
            self->closeSocket();
            self->notifyClose(std::nullopt);
        };
        if (auto* tcp = std::get_if<TcpWs>(&ws_)) {
            tcp->async_close(closeReason, onClosed);
            return;
        }
        if (auto* ssl = std::get_if<SslWs>(&ws_)) {
            ssl->async_close(closeReason, onClosed);
            return;
        }
        notifyClose(std::nullopt);
    }

    // --- public API (io thread) --------------------------------------------

    void request(const std::string& id, std::string method, nlohmann::json params,
                 std::uint64_t timeoutMs, RequestDone done) {
        if (stage_ != Stage::kReady || !session_) {
            if (done) done(std::nullopt, unavailable("Desktop Orca is not connected"));
            return;
        }
        nlohmann::json frame = {
            {"id", id},
            {"deviceToken", options_.deviceToken},
            {"method", method},
        };
        if (!params.is_null()) frame["params"] = std::move(params);

        const std::uint64_t timeout = timeoutMs == 0 ? options_.requestTimeoutMs : timeoutMs;
        auto timer = std::make_shared<asio::steady_timer>(io_);
        timer->expires_after(std::chrono::milliseconds(timeout));
        auto self = shared_from_this();
        timer->async_wait([self, id, method, timeout](boost::system::error_code ec) {
            if (ec) return;
            const auto it = self->pending_.find(id);
            if (it == self->pending_.end()) return;
            RequestDone timedOut = std::move(it->second.done);
            self->pending_.erase(it);
            if (timedOut) {
                timedOut(std::nullopt,
                         LinkError("runtime_timeout",
                                   method + " timed out after " + std::to_string(timeout) + "ms"));
            }
        });
        pending_[id] = PendingRequest{std::move(done), std::move(timer)};
        sendSealed(frame);
    }

    void subscribe(const std::string& id, std::string method, nlohmann::json params,
                   FrameSink sink) {
        if (stage_ != Stage::kReady || !session_) return;
        nlohmann::json frame = {
            {"id", id},
            {"deviceToken", options_.deviceToken},
            {"method", method},
        };
        if (!params.is_null()) frame["params"] = std::move(params);
        subscriptions_[id] = std::move(sink);
        sendSealed(frame);
    }

    void unsubscribe(const std::string& id) { subscriptions_.erase(id); }

    bool open() const { return stage_ == Stage::kReady; }
};

OrcaConnection::OrcaConnection(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

void OrcaConnection::connect(asio::io_context& io, Options options, CloseSink onClose,
                             ConnectDone done) {
    auto impl = std::make_shared<Impl>(io, std::move(options), std::move(onClose),
                                       std::move(done));
    impl->start();
}

void OrcaConnection::request(const std::string& id, std::string method,
                             nlohmann::json params, std::uint64_t timeoutMs,
                             RequestDone done) {
    impl_->request(id, std::move(method), std::move(params), timeoutMs, std::move(done));
}

void OrcaConnection::subscribe(const std::string& id, std::string method,
                               nlohmann::json params, FrameSink sink) {
    impl_->subscribe(id, std::move(method), std::move(params), std::move(sink));
}

void OrcaConnection::unsubscribe(const std::string& id) { impl_->unsubscribe(id); }

void OrcaConnection::close(std::string reason) {
    impl_->closeGracefully(reason);
}

bool OrcaConnection::open() const { return impl_->open(); }

}  // namespace orca::gateway
