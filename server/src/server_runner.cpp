#include <orca/server/server_runner.hpp>

#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <fstream>
#include <sstream>

namespace orca::server {

namespace beast = boost::beast;
namespace asio = boost::asio;
namespace http = beast::http;
namespace websocket = beast::websocket;

namespace {

constexpr int kHelloTimeoutSeconds = 5;
constexpr std::size_t kMaxHttpBodyBytes = 64 * 1024;

std::optional<std::string> headerOf(const http::request<http::string_body>& request,
                                    const char* name) {
    const auto it = request.find(name);
    if (it == request.end()) return std::nullopt;
    return std::string(it->value());
}

bool wantsUpgrade(const http::request<http::string_body>& request) {
    return beast::websocket::is_upgrade(request);
}

// The closest trusted proxy appends the real client last; earlier entries are
// attacker-controlled and would let a spoofer rotate past the failure limit.
std::string lastForwardedHop(const std::string& value) {
    std::string last;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t comma = value.find(',', begin);
        const std::size_t end = comma == std::string::npos ? value.size() : comma;
        std::string hop = value.substr(begin, end - begin);
        while (!hop.empty() && (hop.front() == ' ' || hop.front() == '\t')) hop.erase(hop.begin());
        while (!hop.empty() && (hop.back() == ' ' || hop.back() == '\t')) hop.pop_back();
        if (!hop.empty()) last = hop;
        if (comma == std::string::npos) break;
        begin = comma + 1;
    }
    return last;
}

}  // namespace

// HttpConnection: HTTP until an upgrade switches the stream to WsSession.
// ---------------------------------------------------------------------------

class ServerRunner::HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    HttpConnection(ServerRunner& runner, asio::ip::tcp::socket socket, std::uint64_t id,
                   std::string clientIp)
        : runner_(runner),
          stream_(std::move(socket)),
          id_(id),
          clientIp_(std::move(clientIp)),
          request_(std::make_shared<http::request<http::string_body>>()) {}

    void start() { readRequest(); }

private:
    void readRequest() {
        // A fresh parser per request; both it and the buffer live in this
        // object, which the lambda keeps alive for the duration of the op.
        request_ = std::make_shared<http::request<http::string_body>>();
        auto self = shared_from_this();
        http::async_read(stream_, buffer_, *request_,
                         [this, self](boost::system::error_code ec, std::size_t) {
                             if (ec) {
                                 // Normal end: the peer closed a keep-alive connection.
                                 return;
                             }
                             handleRequest();
                         });
    }

    void handleRequest() {
        const std::string target(request_->target());
        if (wantsUpgrade(*request_)) {
            if (target == "/gateway") {
                upgrade(WsSession::Role::kGateway);
                return;
            }
            if (target == "/client") {
                // Origin check first (403), then the session cookie (401).
                if (runner_.config_.publicOrigin) {
                    const auto origin = headerOf(*request_, "Origin");
                    if (!origin || *origin != *runner_.config_.publicOrigin) {
                        respondText(http::status::forbidden, "");
                        return;
                    }
                }
                const auto cookie = headerOf(*request_, "Cookie");
                const auto token = readCookie(cookie, SessionStore::kCookieName);
                if (!token || !runner_.sessions_.touch(*token)) {
                    respondText(http::status::unauthorized, "");
                    return;
                }
                upgrade(WsSession::Role::kClient);
                return;
            }
            respondText(http::status::not_found, "");
            return;
        }

        // Plain HTTP. Bodies above the limit are refused before parsing.
        std::optional<std::string> body;
        const std::string method(request_->method_string());
        const auto contentLengthField = request_->find(http::field::content_length);
        const std::size_t contentLength =
            contentLengthField == request_->end()
                ? 0
                : static_cast<std::size_t>(
                      std::stoull(std::string(contentLengthField->value())));
        if (contentLength > kMaxHttpBodyBytes) {
            respondText(http::status::payload_too_large, "payload too large\n");
            return;
        }
        if (contentLength > 0) body = request_->body();

        std::string clientIp = clientIp_;
        if (runner_.config_.trustProxy) {
            const auto forwarded = headerOf(*request_, "X-Forwarded-For");
            if (forwarded) {
                const std::string hop = lastForwardedHop(*forwarded);
                if (!hop.empty()) clientIp = hop;
            }
        }

        const auto cookie = headerOf(*request_, "Cookie");
        HttpResponse response = runner_.app_.handle(method, target, cookie, body, clientIp);
        if (response.fileTarget) {
            std::ifstream in(*response.fileTarget, std::ios::binary);
            if (!in) {
                response.status = 404;
                response.body = "not found\n";
                response.headers.clear();
                response.headers.emplace_back("Content-Type", "text/plain; charset=utf-8");
            } else {
                std::ostringstream data;
                data << in.rdbuf();
                response.body = data.str();
                // Without a Content-Type (and with nosniff set below) browsers
                // refuse to execute module scripts or apply stylesheets.
                response.headers.emplace_back("Content-Type", response.fileContentType);
                if (response.fileNoStore) {
                    response.headers.emplace_back("Cache-Control", "no-store");
                }
            }
        }

        auto out = std::make_shared<http::response<http::string_body>>(
            static_cast<http::status>(response.status), request_->version());
        out->body() = response.body;
        out->prepare_payload();
        for (const auto& [name, value] : response.headers) out->set(name, value);
        const bool keepAlive = request_->keep_alive();
        out->keep_alive(keepAlive);
        auto self = shared_from_this();
        http::async_write(stream_, *out,
                          [this, self, out, keepAlive](boost::system::error_code ec, std::size_t) {
                              if (ec || !keepAlive) {
                                  beast::get_lowest_layer(stream_).cancel();
                                  return;
                              }
                              readRequest();
                          });
    }

    void respondText(http::status status, const std::string& body) {
        auto out = std::make_shared<http::response<http::string_body>>(status, request_->version());
        out->body() = body;
        out->prepare_payload();
        auto self = shared_from_this();
        http::async_write(stream_, *out,
                          [this, self, out](boost::system::error_code ec, std::size_t) {
                              if (ec) return;
                              // The reject paths close: the client must see the status.
                              beast::get_lowest_layer(stream_).cancel();
                          });
    }

        void upgrade(WsSession::Role role) {
        // The websocket session takes over the stream; the HTTP object dies
        // once this handler returns (its op state is complete).
        auto session = std::make_shared<WsSession>(runner_, std::move(stream_), id_, role,
                                                   std::move(*request_));
        runner_.wsSessions_[id_] = session;
        session->start();
    }

    ServerRunner& runner_;
    boost::beast::tcp_stream stream_;
    std::uint64_t id_;
    std::string clientIp_;
    std::shared_ptr<http::request<http::string_body>> request_;
    boost::beast::flat_buffer buffer_;
};

// ---------------------------------------------------------------------------
// WsSession
// ---------------------------------------------------------------------------

ServerRunner::WsSession::WsSession(ServerRunner& runner, boost::beast::tcp_stream stream,
                                   std::uint64_t id, Role role,
                                   http::request<http::string_body> upgradeRequest)
    : runner_(runner),
      ws_(std::move(stream)),
      id_(id),
      role_(role),
      upgradeRequest_(std::move(upgradeRequest)) {}

ServerRunner::WsSession::~WsSession() {
    boost::system::error_code ec;
    beast::get_lowest_layer(ws_).close();
}

void ServerRunner::WsSession::start() {
    ws_.text(true);
    auto self = shared_from_this();
    ws_.async_accept(upgradeRequest_, [this, self](boost::system::error_code ec) {
        if (ec) {
            finish();
            return;
        }
        std::vector<HubAction> actions = role_ == Role::kGateway
                                             ? runner_.hub_.onGatewayAttached(id_)
                                             : runner_.hub_.onClientAttached(id_);
        runner_.dispatchHub(actions);
        if (role_ == Role::kGateway) {
            helloTimer_.emplace(runner_.io_, std::chrono::seconds(kHelloTimeoutSeconds));
            helloTimer_->async_wait([this, self](boost::system::error_code waitEc) {
                if (waitEc) return;  // cancelled on admission
                if (!runner_.hub_.isAdmittedGateway(id_)) {
                    for (const HubAction& action : runner_.hub_.helloTimeoutActions(id_)) {
                        if (action.kind == HubAction::Kind::kSend) sendText(action.json);
                    }
                    closeWebSocket(4001, "hello timeout");
                }
            });
        }
        readLoop();
    });
}

void ServerRunner::WsSession::readLoop() {
    auto self = shared_from_this();
    ws_.async_read(buffer_, [this, self](boost::system::error_code ec, std::size_t) {
        if (ec) {
            finish();
            return;
        }
        onFrame();
        if (!closed_) readLoop();
    });
}

void ServerRunner::WsSession::onFrame() {
    if (!ws_.got_text()) {
        buffer_.consume(buffer_.size());
        return;  // binary frames are ignored, like the TS JSON.parse try/catch
    }
    std::string payload = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    try {
        const nlohmann::json frame = nlohmann::json::parse(payload);
        const std::vector<HubAction> actions = role_ == Role::kGateway
                                                   ? runner_.hub_.onGatewayFrame(id_, frame)
                                                   : runner_.hub_.onClientFrame(id_, frame);
        runner_.dispatchHub(actions);
    } catch (const nlohmann::json::exception&) {
        // malformed frames are dropped silently
    }
}

void ServerRunner::WsSession::sendText(std::string frame) {
    if (closed_) return;
    writeQueue_.push_back(std::move(frame));
    pumpWrites();
}

void ServerRunner::WsSession::pumpWrites() {
    if (writing_ || closed_ || writeQueue_.empty()) return;
    writing_ = true;
    // The queued string stays alive: the deque owns it until the op completes.
    ws_.text(true);
    auto self = shared_from_this();
    ws_.async_write(asio::buffer(writeQueue_.front()),
                    [this, self](boost::system::error_code ec, std::size_t) {
                        writing_ = false;
                        if (!writeQueue_.empty()) writeQueue_.pop_front();
                        if (ec) {
                            finish();
                            return;
                        }
                        pumpWrites();
                    });
}

void ServerRunner::WsSession::closeWebSocket(std::uint16_t code, const std::string& reason) {
    if (closed_) return;
    closed_ = true;
    websocket::close_reason closeReason;
    closeReason.code = code;
    closeReason.reason = reason;
    auto self = shared_from_this();
    ws_.async_close(closeReason, [this, self](boost::system::error_code) { finish(); });
}

void ServerRunner::WsSession::finish() {
    if (helloTimer_) helloTimer_.reset();
    const bool wasGateway = role_ == Role::kGateway;
    const bool wasAdmitted = runner_.hub_.isAdmittedGateway(id_);
    if (!wasGateway) runner_.wsSessions_.erase(id_);
    std::vector<HubAction> actions;
    if (wasGateway) {
        if (wasAdmitted) actions = runner_.hub_.onGatewayDropped(id_);
    } else {
        actions = runner_.hub_.onClientDropped(id_);
    }
    for (const HubAction& action : actions) {
        if (action.kind != HubAction::Kind::kSend) continue;
        const auto it = runner_.wsSessions_.find(action.connectionId);
        if (it != runner_.wsSessions_.end()) it->second->sendText(action.json);
    }
}


// ---------------------------------------------------------------------------
// ServerRunner
// ---------------------------------------------------------------------------

ServerRunner::ServerRunner(asio::io_context& io, ServerConfig config, SessionStore& sessions,
                           HubCore& hub, HttpApp& app)
    : io_(io), config_(std::move(config)), sessions_(sessions), hub_(hub), app_(app) {}

bool ServerRunner::listen() {
    boost::system::error_code ec;
    const auto address = asio::ip::make_address(config_.host, ec);
    if (ec) return false;
    acceptor_.emplace(io_, asio::ip::tcp::endpoint(address, config_.port));
    acceptor_->set_option(asio::socket_base::reuse_address(true), ec);
    if (ec) {
        acceptor_.reset();
        return false;
    }
    doAccept();
    return true;
}

void ServerRunner::doAccept() {
    if (shuttingDown_ || !acceptor_) return;
    // The accept handler keeps the accepted socket alive through the shared
    // HttpConnection; nothing here touches a moved-from socket.
    acceptor_->async_accept([this](boost::system::error_code ec,
                                   asio::ip::tcp::socket socket) {
        if (shuttingDown_) return;
        if (ec) {
            doAccept();
            return;
        }
        const auto remote = socket.remote_endpoint();
        const std::uint64_t id = hub_.nextConnectionId();
        auto connection = std::make_shared<HttpConnection>(*this, std::move(socket), id,
                                                           remote.address().to_string());
        connection->start();
        doAccept();
    });
}

void ServerRunner::run() {
    io_.run();
}

void ServerRunner::dispatchHub(const std::vector<HubAction>& actions) {
    for (const HubAction& action : actions) {
        const auto it = wsSessions_.find(action.connectionId);
        if (it == wsSessions_.end()) continue;
        if (action.kind == HubAction::Kind::kSend) {
            it->second->sendText(action.json);
        } else if (action.kind == HubAction::Kind::kClose) {
            it->second->closeWebSocket(action.closeCode, action.closeReason);
        }
    }
}

void ServerRunner::shutdown(const std::string& reason) {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    boost::system::error_code ec;
    if (acceptor_) acceptor_->close(ec);
    // Browsers and the gateway must see an explicit close rather than a dead
    // socket; shutdownActions covers every live connection the hub knows of.
    for (const HubAction& action : hub_.shutdownActions()) {
        const auto it = wsSessions_.find(action.connectionId);
        if (it == wsSessions_.end()) continue;
        if (action.kind == HubAction::Kind::kSend) {
            it->second->sendText(action.json);
        } else {
            it->second->closeWebSocket(action.closeCode, action.closeReason);
        }
    }
    shutdownTimer_ = std::make_unique<asio::steady_timer>(io_, std::chrono::seconds(3));
    shutdownTimer_->async_wait([this, reason](boost::system::error_code) { io_.stop(); });
    (void)reason;
}

// ---------------------------------------------------------------------------

}  // namespace orca::server
