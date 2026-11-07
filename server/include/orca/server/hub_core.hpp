#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace orca::server {

// Pure routing state machine for the hub, mirrored from
// apps/server/src/hub.ts. The IO layer feeds connection events in and executes
// the returned actions out; no sockets live here, so every behaviour below is
// unit-testable:
//   - gateway admission by hello key (SHA-256, constant time) and replacement
//   - browser request id <-> gateway request id mapping, pending failure on
//     gateway drop/replacement
//   - subscription tables with full replay + stream-reset on gateway reconnect
//   - orca-state broadcast, app-level ping/pong
// The hub is a forwarder: method/params/result/error pass through verbatim.

struct HubAction {
    enum class Kind {
        kSend,   // json text frame to connectionId
        kClose,  // close connectionId with code/reason
    };
    Kind kind = Kind::kSend;
    std::uint64_t connectionId = 0;
    std::string json;        // kSend payload (one JSON text frame)
    std::uint16_t closeCode = 1000;  // kClose websocket status code
    std::string closeReason;
};

class HubCore {
public:
    HubCore(std::string gatewayKey, std::function<void(const std::string&)> log);

    // Every connection (gateway or browser) gets a unique id from here; the
    // admitted gateway is whichever id passed the hello handshake last.
    std::uint64_t nextConnectionId() noexcept { return nextClientId_++; }

    // JSON blob for /api/health: {connected, orcaState, orca?}.
    nlohmann::json getGatewayState() const;

    bool isAdmittedGateway(std::uint64_t connectionId) const noexcept;

    // Connection lifecycle. IO owns the hello timeout: when it fires for a
    // non-admitted connection, emit helloTimeoutActions(id).
    std::vector<HubAction> onGatewayAttached(std::uint64_t connectionId);
    std::vector<HubAction> onGatewayFrame(std::uint64_t connectionId, const nlohmann::json& frame);
    std::vector<HubAction> onGatewayDropped(std::uint64_t connectionId);
    std::vector<HubAction> helloTimeoutActions(std::uint64_t connectionId) const;
    std::vector<HubAction> onClientAttached(std::uint64_t connectionId);
    std::vector<HubAction> onClientFrame(std::uint64_t connectionId, const nlohmann::json& frame);
    std::vector<HubAction> onClientDropped(std::uint64_t connectionId);
    // Graceful shutdown: explicit 1001 closes so peers see intent, not death.
    std::vector<HubAction> shutdownActions() const;

private:
    struct Subscription {
        std::uint64_t clientConnectionId = 0;
        std::string localId;
        std::string method;
        nlohmann::json params;  // null when the client omitted params
        bool hasParams = false;
    };
    struct Pending {
        std::uint64_t clientConnectionId = 0;
        std::string localId;
    };
    struct ClientSubscription {
        std::string gatewayId;
        std::string method;
        nlohmann::json params;
        bool hasParams = false;
    };

    std::string newGatewayId(std::uint64_t clientConnectionId);
    bool verifyKey(const nlohmann::json& key) const;
    std::vector<HubAction> admitGateway(std::uint64_t connectionId, const nlohmann::json& hello);
    std::vector<HubAction> failPending(const std::string& code, const std::string& message);
    std::vector<HubAction> broadcastState(bool connected,
                                          const std::optional<std::string>& message) const;

    std::string gatewayKey_;
    std::function<void(const std::string&)> log_;

    bool gatewayAdmitted_ = false;
    std::uint64_t gatewayConnectionId_ = 0;
    std::string gatewayId_;
    nlohmann::json orcaState_ = nullptr;   // "connecting"|"connected"|"disconnected"|null
    nlohmann::json descriptor_ = nullptr;  // hello.orca descriptor or null
    std::map<std::uint64_t, bool> connectedClients_;  // connId -> present
    std::map<std::uint64_t, std::map<std::string, ClientSubscription>> clientSubscriptions_;
    std::map<std::string, Pending> pending_;          // gateway request id -> owner
    std::map<std::string, Subscription> subscriptions_;  // gateway request id -> subscription
    std::uint64_t nextClientId_ = 1;    // browser connection numbering
    std::uint64_t nextGatewaySeq_ = 1;  // hub-owned forwarded request ids
};

}  // namespace orca::server
