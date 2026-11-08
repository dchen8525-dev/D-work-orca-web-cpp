#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace orca::gateway {

// Pure decision logic of the outbound hub link, mirrored from
// apps/gateway/src/hub-link.ts. The IO layer (hub_client) executes the
// returned actions; timings and frame handling here are unit-testable.

struct LinkAction {
    enum class Kind {
        kSend,  // json text frame upstream
        kClose,  // close the websocket with code/reason
        kLog,   // record a message
    };
    Kind kind = Kind::kSend;
    std::string json;        // kSend payload
    std::uint16_t closeCode = 1000;  // kClose
    std::string closeReason;
    std::string message;     // kLog
};

class HubLinkCore {
public:
    static constexpr int kPingIntervalMs = 25'000;
    static constexpr int kSilenceTimeoutMs = 75'000;
    static constexpr std::uint64_t kRetryBaseMs = 500;
    static constexpr std::uint64_t kRetryMaxMs = 30'000;

    HubLinkCore(std::string gatewayKey, std::string gatewayId);

    // The first frame of every connection. orca may be null (P4-a: no Orca link).
    nlohmann::json helloFrame(const nlohmann::json& orcaState, const nlohmann::json& orca) const;

    // Frames coming DOWN from the hub. request/subscribe/unsubscribe are
    // forwarded to the Orca bridge by the IO layer (they need the bridge), so
    // they surface here as kForward* markers the client switches on.
    enum class DownKind { kIgnore, kSendPong, kLog, kCloseRejected, kForwardRequest,
                          kForwardSubscribe, kForwardUnsubscribe };
    struct DownDecision {
        DownKind kind = DownKind::kIgnore;
        std::string message;      // kLog
        std::uint16_t closeCode = 1000;  // kCloseRejected
        std::string id;           // forward target id
        std::string method;
        nlohmann::json params;    // null when the hub omitted params
        bool hasParams = false;
        std::uint64_t timeoutMs = 0;
        bool hasTimeout = false;
    };
    DownDecision onHubFrame(const nlohmann::json& frame) const;

    // hub-link.ts backoffMs(): exponential 500ms -> 30s, +/-20% jitter.
    static std::uint64_t backoffMs(int attempt, double jitter01);

    static bool isSilent(std::uint64_t nowMs, std::uint64_t lastFrameAtMs) {
        return nowMs - lastFrameAtMs >= static_cast<std::uint64_t>(kSilenceTimeoutMs);
    }

private:
    std::string gatewayKey_;
    std::string gatewayId_;
};

}  // namespace orca::gateway
