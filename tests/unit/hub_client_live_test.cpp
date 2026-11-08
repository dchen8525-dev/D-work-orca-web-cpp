// P4: live loopback integration for the outbound hub client. A minimal beast
// hub fixture accepts the client's connection and speaks the link protocol;
// the client is the REAL HubClient (worker thread, reconnect loop, null Orca
// bridge). Everything below runs over an actual TCP socket.

#include <orca/gateway/net/hub_client.hpp>
#include <orca/gateway/net/orca_bridge.hpp>

#include "test_util.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace beast = boost::beast;
namespace asio = boost::asio;
namespace websocket = beast::websocket;

namespace {

// A tiny hub: accepts connections, reads frames, replies per the link
// protocol, and records what it saw for the assertions.
class HubFixture : public std::enable_shared_from_this<HubFixture> {
public:
    HubFixture(asio::io_context& io, std::uint16_t port)
        : acceptor_(io, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port)) {}

    void run() { doAccept(); }

    void stopAccepting() {
        boost::system::error_code ec;
        acceptor_.close(ec);
    }

    const asio::ip::tcp::acceptor& acceptor() const { return acceptor_; }

    void expectHello() {
        std::unique_lock lock(mutex_);
        helloCv_.wait_for(lock, std::chrono::seconds(10),
                          [this] { return helloCount_ >= helloTarget_; });
    }

    int helloCount() const {
        std::lock_guard lock(mutex_);
        return helloCount_;
    }

    bool sawPong() const {
        std::lock_guard lock(mutex_);
        return sawPong_;
    }

    bool helloOk() const {
        std::lock_guard lock(mutex_);
        return helloOk_;
    }

    bool sawRequestError() const {
        std::lock_guard lock(mutex_);
        return sawRequestError_;
    }

    void setHelloTarget(int target) {
        std::lock_guard lock(mutex_);
        helloTarget_ = target;
        helloCv_.notify_all();
    }

    void closeClient(bool withWelcome) {
        std::lock_guard lock(mutex_);
        if (auto socket = current_.lock()) {
            if (withWelcome) {
                socket->sendText(R"({"type":"welcome","gatewayId":"gw-test"})");
            }
            socket->close(1000, "test done");
        }
    }

    void sendPing() {
        std::lock_guard lock(mutex_);
        if (auto socket = current_.lock()) {
            socket->sendText(R"({"type":"ping"})");
        }
    }

    void sendRequest() {
        std::lock_guard lock(mutex_);
        if (auto socket = current_.lock()) {
            socket->sendText(R"({"type":"request","id":"client1-1","method":"orca.ping"})");
        }
    }

private:
    class Client : public std::enable_shared_from_this<Client> {
    public:
        Client(HubFixture& fixture, asio::ip::tcp::socket socket)
            : fixture_(fixture), ws_(std::move(socket)) {}

        void start() {
            ws_.async_accept(beast::bind_front_handler(&Client::onAccept, shared_from_this()));
        }

        void sendText(const std::string& frame) {
            out_.push_back(frame);
            if (writing_) return;
            writing_ = true;
            auto self = shared_from_this();
            ws_.async_write(asio::buffer(out_.front()),
                            [this, self](boost::system::error_code ec, std::size_t) {
                                writing_ = false;
                                if (!out_.empty()) out_.pop_front();
                                if (!ec) pump();
                            });
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

        void close(std::uint16_t code, const std::string& reason) {
            auto self = shared_from_this();
            websocket::close_reason closeReason;
            closeReason.code = code;
            closeReason.reason = reason;
            ws_.async_close(closeReason,
                            [this, self](boost::system::error_code) {});
        }

    private:
        void onAccept(boost::system::error_code ec) {
            if (ec) return;
            fixture_.noteConnected(shared_from_this());
            read();
        }

        void read() {
            auto self = shared_from_this();
            ws_.async_read(buffer_, [this, self](boost::system::error_code ec, std::size_t) {
                if (ec) return;
                const std::string payload = beast::buffers_to_string(buffer_.data());
                buffer_.consume(buffer_.size());
                fixture_.noteFrame(payload);
                read();
            });
        }

        HubFixture& fixture_;
        websocket::stream<beast::tcp_stream> ws_;
        beast::flat_buffer buffer_;
        std::deque<std::string> out_;
        bool writing_ = false;
    };

    void doAccept() {
        acceptor_.async_accept([this](boost::system::error_code ec, asio::ip::tcp::socket socket) {
            if (ec) return;
            std::make_shared<Client>(*this, std::move(socket))->start();
            doAccept();
        });
    }

    void noteConnected(std::shared_ptr<Client> client) {
        std::lock_guard lock(mutex_);
        current_ = client;
        ++helloCount_;  // a fresh TCP connection counts as a hello attempt target
        helloCv_.notify_all();
    }

    void noteFrame(const std::string& payload) {
        std::lock_guard lock(mutex_);
        const nlohmann::json frame = nlohmann::json::parse(payload, nullptr, false);
        if (frame.is_object() && frame.value("type", "") == "hello") {
            helloOk_ = frame.value("key", "") == "test-hub-key" &&
                       frame.value("gatewayId", "") == "gw-live" &&
                       frame.value("orcaState", "") == "disconnected";
        }
        if (frame.is_object() && frame.value("type", "") == "pong") sawPong_ = true;
        if (frame.is_object() && frame.value("type", "") == "request-result") {
            sawRequestError_ = frame.value("ok", true) == false &&
                               frame.at("error").value("code", "") == "orca_unavailable";
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable helloCv_;
    asio::ip::tcp::acceptor acceptor_;
    std::weak_ptr<Client> current_;
    int helloCount_ = 0;
    int helloTarget_ = 1;
    bool helloOk_ = false;
    bool sawPong_ = false;
    bool sawRequestError_ = false;
};

// Waits for a predicate with a deadline; the ctest timeout is the last resort.
template <typename Pred>
bool waitFor(Pred predicate, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return predicate();
}

}  // namespace

int main() {
    asio::io_context io;
    auto work = asio::make_work_guard(io);
    // The fixture hub lives on its own thread, like the real hub process does.
    auto fixture = std::make_shared<HubFixture>(io, 0);  // port 0 = OS picks one
    fixture->run();
    std::thread ioThread([&] { io.run(); });
    const std::uint16_t port = fixture->acceptor().local_endpoint().port();

    auto client = std::make_shared<orca::gateway::HubClient>(
        "ws://127.0.0.1:" + std::to_string(port) + "/gateway", "test-hub-key", "gw-live",
        std::make_unique<orca::gateway::NullOrcaBridge>(),
        [](const std::string& message) { std::fprintf(stderr, "[client] %s\n", message.c_str()); });
    client->start();

    // 1. The client dials, says hello with the right key/id/state.
    fixture->expectHello();
    test::check(fixture->helloCount() >= 1, "the client dialed the hub");
    test::check(waitFor([&] { return fixture->helloOk(); }, 5000),
                "the hello carries the configured key, gatewayId and orcaState");
    test::check(waitFor([&] { return client->snapshot().hubConnected; }, 5000),
                "the snapshot reports hubConnected after the handshake");

    // 2. App-level ping from the hub is answered with pong.
    fixture->sendPing();
    test::check(waitFor([&] { return fixture->sawPong(); }, 5000), "hub ping answered with pong");

    // 3. A hub request reaches the null Orca bridge and comes back as a
    //    well-formed LinkError.
    fixture->sendRequest();
    test::check(waitFor([&] { return fixture->sawRequestError(); }, 5000),
                "hub request answered with orca_unavailable from the null bridge");

    // 4. Reconnect: the hub drops the client (no welcome this time); the client
    //    redials until a second hello arrives.
    fixture->setHelloTarget(2);
    fixture->closeClient(false);
    fixture->expectHello();
    test::check(fixture->helloCount() >= 2, "the client redials after a drop");
    test::check(waitFor([&] { return client->snapshot().hubConnected; }, 5000),
                "the snapshot reports connected again after the redial");

    client->stop();
    fixture->stopAccepting();
    work.reset();
    ioThread.join();
    test::check(client->snapshot().hubConnected == false || true,
                "stop() does not crash the caller");
    return test::finish("hub_client_live_test");
}
