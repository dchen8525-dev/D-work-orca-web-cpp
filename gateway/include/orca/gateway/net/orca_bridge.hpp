#pragma once

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace orca::gateway {

// Bridge between the hub link and the desktop Orca connection
// (apps/gateway/src/orca-bridge.ts). The real JSON-RPC bridge is P2; P4-a
// ships a null implementation so the hub link speaks the full protocol while
// honestly reporting the Orca side as disconnected.

enum class OrcaState { kConnecting, kConnected, kDisconnected };

inline const char* orcaStateName(OrcaState state) {
    switch (state) {
        case OrcaState::kConnecting: return "connecting";
        case OrcaState::kConnected: return "connected";
        case OrcaState::kDisconnected: return "disconnected";
    }
    return "disconnected";
}

// The link-layer error shape (link-protocol.ts LinkError).
struct LinkError : std::runtime_error {
    std::string code;
    LinkError(std::string code_, const std::string& message)
        : std::runtime_error(message), code(std::move(code_)) {}
};

struct BridgeEvent {
    enum class Kind { kState, kOrca, kStreamFrame, kStreamError, kStreamReset };
    Kind kind = Kind::kState;
    std::string state;      // kState
    std::string message;    // kState optional
    std::string id;         // stream kinds
    nlohmann::json payload;  // result / error / descriptor
};

class OrcaBridge {
public:
    virtual ~OrcaBridge() = default;

    virtual OrcaState state() const = 0;
    // The Orca app descriptor (appVersion, machineName, ...); null when absent.
    virtual nlohmann::json descriptor() const = 0;
    // Synchronous request into Orca; throws LinkError on failure.
    virtual nlohmann::json request(const std::string& method, const nlohmann::json& params,
                                   std::uint64_t timeoutMs) = 0;
    virtual void subscribe(const std::string& id, const std::string& method,
                           const nlohmann::json& params) = 0;
    virtual void unsubscribe(const std::string& id) = 0;
    // The hub re-issues subscriptions after reconnecting; drop ours so nothing
    // stale streams.
    virtual void unsubscribeAll() = 0;
};

// P4-a placeholder: the Orca JSON-RPC connection (P2) is not built yet, so
// every request fails with a well-formed error and state stays disconnected.
class NullOrcaBridge final : public OrcaBridge {
public:
    OrcaState state() const override { return OrcaState::kDisconnected; }
    nlohmann::json descriptor() const override { return nlohmann::json(nullptr); }
    nlohmann::json request(const std::string& /*method*/, const nlohmann::json& /*params*/,
                           std::uint64_t /*timeoutMs*/) override {
        throw LinkError("orca_unavailable", "The Orca connection is not implemented yet (P2)");
    }
    void subscribe(const std::string& /*id*/, const std::string& /*method*/,
                   const nlohmann::json& /*params*/) override {}
    void unsubscribe(const std::string& /*id*/) override {}
    void unsubscribeAll() override {}
};

}  // namespace orca::gateway
