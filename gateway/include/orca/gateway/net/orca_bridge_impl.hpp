#pragma once

#include <orca/gateway/net/orca_bridge.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace orca::gateway {

class OrcaConnection;

struct OrcaBridgeOptions {
    std::string endpoint;
    std::string deviceToken;
    std::string hostPublicKeyB64;
    std::string transport = "direct";  // "direct" | "relay"
    std::string relayHostId;
};

// Real bridge (apps/gateway/src/orca-bridge.ts): keeps one live Orca
// connection for the hub, reconnects with 500ms -> 30s jittered backoff,
// re-advertises capabilities per socket and replays every subscription.
//
// The bridge owns a worker thread; every Orca callback runs there. Public
// methods may be called from any thread and marshal onto it.
class OrcaBridgeImpl final : public OrcaBridge {
public:
    OrcaBridgeImpl(OrcaBridgeOptions options, std::function<void(const std::string&)> log);
    ~OrcaBridgeImpl() override;

    void start();

    void setEventSink(EventSink sink) override;
    OrcaState state() const override;
    nlohmann::json descriptor() const override;
    void request(const std::string& id, const std::string& method,
                 const nlohmann::json& params, std::uint64_t timeoutMs,
                 RequestDone done) override;
    void subscribe(const std::string& id, const std::string& method,
                   const nlohmann::json& params) override;
    void unsubscribe(const std::string& id) override;
    void unsubscribeAll() override;
    void shutdown() override;

private:
    struct Subscription {
        std::string method;
        nlohmann::json params;
    };

    void connectOnce();
    void onConnectFailed(const LinkError& error);
    void onConnectionClosed(std::optional<LinkError> error);
    void readDescriptor();
    void replaySubscriptions();
    void openSubscription(const std::string& id, const Subscription& subscription);
    void scheduleRetry();
    void setState(OrcaState state, std::string message);
    void publish(const BridgeEvent& event);
    void log(const std::string& message) const;

    OrcaBridgeOptions options_;
    std::function<void(const std::string&)> log_;

    boost::asio::io_context io_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    std::thread worker_;
    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};

    // Guarded by mutex_: read by the hub thread (hello / status card).
    mutable std::mutex mutex_;
    OrcaState state_ = OrcaState::kConnecting;
    nlohmann::json descriptor_ = nlohmann::json(nullptr);
    EventSink sink_;

    // Worker-thread only.
    std::shared_ptr<OrcaConnection> connection_;
    std::map<std::string, Subscription> subscriptions_;
    std::optional<boost::asio::steady_timer> retryTimer_;
    int attempt_ = 0;
};

}  // namespace orca::gateway
