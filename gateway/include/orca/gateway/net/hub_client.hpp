#pragma once

#include <orca/gateway/net/hub_link_core.hpp>
#include <orca/gateway/net/orca_bridge.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>

namespace orca::gateway {

// Outbound connection to the VPS hub (apps/gateway/src/hub-link.ts). The
// gateway never listens on a public port; this client dials the hub, says
// hello, answers app-level pings and shuttles request/subscription traffic
// between the hub and the Orca bridge.
//
// Threading: the client owns a worker thread with its own asio io_context; all
// socket handlers run there. The Qt thread only touches snapshot(), which is
// mutex-guarded. Reconnects follow hub-link.ts: exponential backoff 500ms ->
// 30s with jitter, reset on a completed handshake; the hub re-issues
// subscriptions after reconnecting.
class HubClient {
public:
    struct Snapshot {
        bool hubConnected = false;
        std::string hubUrl;
        std::string lastError;
        int attempts = 0;
    };

    HubClient(std::string hubUrl, std::string gatewayKey, std::string gatewayId,
              std::unique_ptr<OrcaBridge> bridge, std::function<void(const std::string&)> log);
    ~HubClient();

    HubClient(const HubClient&) = delete;
    HubClient& operator=(const HubClient&) = delete;

    void start();
    void stop();

    Snapshot snapshot() const;

public:
    struct ParsedUrl {
        bool secure = false;
        std::string host;
        std::string port;
        std::string path;
    };

private:
    using TcpWs = boost::beast::websocket::stream<boost::asio::ip::tcp::socket>;
    using SslWs = boost::beast::websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;
    using WsVariant = std::variant<std::monostate, TcpWs, SslWs>;

    void threadEntry();
    void beginAttempt();
    void onResolved(boost::system::error_code ec,
                    boost::asio::ip::tcp::resolver::results_type results);
    void startWsHandshake();
    void onHandshake(boost::system::error_code ec);
    void scheduleRetry();
    void stopRetryTimer();
    void doRead();
    void onRead(boost::system::error_code ec, std::size_t);
    void onPingTimer(boost::system::error_code ec);
    void onFrame();
    void send(const nlohmann::json& frame);
    void pumpWrites();
    void finishAttempt(const std::string& why, bool resetAttempt);
    void teardown(const std::string& why, std::uint16_t closeCode, const std::string& closeReason);
    void handleFrame(const nlohmann::json& frame);
    void handleRequest(const HubLinkCore::DownDecision& decision);
    void handleBridgeEvent(const BridgeEvent& event);
    void startPingTimer();
    void stopPingTimer();
    bool closed() const;

    std::string hubUrl_;
    std::string gatewayKey_;
    std::string gatewayId_;
    std::unique_ptr<OrcaBridge> bridge_;
    std::function<void(const std::string&)> log_;
    HubLinkCore core_;

    boost::asio::io_context io_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    std::optional<boost::asio::ssl::context> sslCtx_;
    boost::asio::ip::tcp::resolver resolver_;
    WsVariant ws_;
    boost::beast::flat_buffer buffer_;
    std::deque<std::string> writeQueue_;
    bool writing_ = false;
    std::optional<boost::asio::steady_timer> pingTimer_;
    std::optional<boost::asio::steady_timer> retryTimer_;
    std::uint64_t lastFrameAtMs_ = 0;
    int attempt_ = 0;
    bool handshakeDone_ = false;
    bool stopped_ = false;
    ParsedUrl url_;

    mutable std::mutex mutex_;
    Snapshot state_;
    std::thread worker_;
};

}  // namespace orca::gateway
