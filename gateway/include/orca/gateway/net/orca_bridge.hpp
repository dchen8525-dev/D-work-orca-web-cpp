#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

namespace orca::gateway {

// Bridge between the hub link and the desktop Orca connection
// (apps/gateway/src/orca-bridge.ts). NullOrcaBridge keeps the hub link honest
// before pairing; OrcaBridgeImpl (P2) is the real reconnecting implementation.

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
    using EventSink = std::function<void(const BridgeEvent& event)>;
    // Exactly one completion per request, on the bridge's worker thread.
    using RequestDone = std::function<void(std::optional<nlohmann::json> result,
                                           std::optional<LinkError> error)>;

    virtual ~OrcaBridge() = default;

    // Events (state changes, stream frames) are delivered on the bridge's own
    // worker thread; the hub link marshals them onto its io_context.
    virtual void setEventSink(EventSink sink) = 0;

    virtual OrcaState state() const = 0;
    // The Orca app descriptor (appVersion, machineName, ...); null when absent.
    virtual nlohmann::json descriptor() const = 0;
    // Asynchronous request into Orca; never blocks the caller.
    virtual void request(const std::string& id, const std::string& method,
                         const nlohmann::json& params, std::uint64_t timeoutMs,
                         RequestDone done) = 0;
    virtual void subscribe(const std::string& id, const std::string& method,
                           const nlohmann::json& params) = 0;
    virtual void unsubscribe(const std::string& id) = 0;
    // The hub re-issues subscriptions after reconnecting; drop ours so nothing
    // stale streams.
    virtual void unsubscribeAll() = 0;
    // Join the bridge worker; safe to call from the owner's destructor.
    virtual void shutdown() = 0;
};

// P4-a placeholder: the Orca JSON-RPC connection (P2) is not built yet, so
// every request fails with a well-formed error and state stays disconnected.
class NullOrcaBridge final : public OrcaBridge {
public:
    void setEventSink(EventSink /*sink*/) override {}
    OrcaState state() const override { return OrcaState::kDisconnected; }
    nlohmann::json descriptor() const override { return nlohmann::json(nullptr); }
    void request(const std::string& /*id*/, const std::string& /*method*/,
                 const nlohmann::json& /*params*/, std::uint64_t /*timeoutMs*/,
                 RequestDone done) override {
        if (done) {
            done(std::nullopt,
                 LinkError("orca_unavailable", "The Orca connection is not paired yet"));
        }
    }
    void subscribe(const std::string& /*id*/, const std::string& /*method*/,
                   const nlohmann::json& /*params*/) override {}
    void unsubscribe(const std::string& /*id*/) override {}
    void unsubscribeAll() override {}
    void shutdown() override {}
};

}  // namespace orca::gateway
