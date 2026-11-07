#pragma once

#include <orca/server/hub_core.hpp>
#include <orca/server/http_app.hpp>
#include <orca/server/server_config.hpp>
#include <orca/server/session_store.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace orca::server {

namespace http = boost::beast::http;

// Owns the acceptor and the wiring between sockets, the HttpApp and the
// HubCore. Single-threaded: one io_context drives everything, matching the TS
// event loop.
//
// Lifetime discipline (the hand-rolled version before this had intermittent
// dead ICP dispatch): every async operation keeps its own state alive through
// shared_ptr captures — the connection object, the parsed request and the
// response being written each outlive the op that uses them.
class ServerRunner {
public:
    ServerRunner(boost::asio::io_context& io, ServerConfig config, SessionStore& sessions,
                 HubCore& hub, HttpApp& app);

    bool listen();  // false when the endpoint cannot be bound
    void run();     // io.run() until stop() or shutdown completes
    void shutdown(const std::string& reason);  // signal path: close all + stop accepting

    // Routes hub-core actions to their target connections by id.
    void dispatchHub(const std::vector<HubAction>& actions);

private:
    class HttpConnection;
    class WsSession;

    void doAccept();

    boost::asio::io_context& io_;
    ServerConfig config_;
    SessionStore& sessions_;
    HubCore& hub_;
    HttpApp& app_;
    std::optional<boost::asio::ip::tcp::acceptor> acceptor_;
    std::map<std::uint64_t, std::shared_ptr<WsSession>> wsSessions_;
    bool shuttingDown_ = false;
    std::unique_ptr<boost::asio::steady_timer> shutdownTimer_;

    friend class WsSession;
};

// Websocket side (gateway or browser client), owning a stream moved from an
// upgraded HttpConnection.
class ServerRunner::WsSession : public std::enable_shared_from_this<WsSession> {
public:
    enum class Role { kGateway, kClient };

    WsSession(ServerRunner& runner, boost::beast::tcp_stream stream, std::uint64_t id, Role role,
              http::request<http::string_body> upgradeRequest);
    ~WsSession();

    void start();
    void sendText(std::string frame);
    void closeWebSocket(std::uint16_t code, const std::string& reason);
    std::uint64_t id() const noexcept { return id_; }

private:
    void onAccepted();
    void readLoop();
    void onFrame();
    void pumpWrites();
    void finish();

    ServerRunner& runner_;
    boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
    std::uint64_t id_;
    Role role_;
    http::request<http::string_body> upgradeRequest_;
    boost::beast::flat_buffer buffer_;
    std::deque<std::string> writeQueue_;
    bool writing_ = false;
    bool closed_ = false;
    std::optional<boost::asio::steady_timer> helloTimer_;
};

}  // namespace orca::server
