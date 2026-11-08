#include <orca/gateway/net/hub_link_core.hpp>

#include <algorithm>
#include <cmath>

namespace orca::gateway {

HubLinkCore::HubLinkCore(std::string gatewayKey, std::string gatewayId)
    : gatewayKey_(std::move(gatewayKey)), gatewayId_(std::move(gatewayId)) {}

nlohmann::json HubLinkCore::helloFrame(const nlohmann::json& orcaState,
                                       const nlohmann::json& orca) const {
    nlohmann::json hello{{"type", "hello"},
                         {"v", 1},  // LINK_PROTOCOL_VERSION
                         {"key", gatewayKey_},
                         {"gatewayId", gatewayId_},
                         {"orcaState", orcaState}};
    if (!orca.is_null()) hello["orca"] = orca;
    return hello;
}

HubLinkCore::DownDecision HubLinkCore::onHubFrame(const nlohmann::json& frame) const {
    DownDecision decision;
    if (!frame.is_object()) return decision;
    const auto typeIt = frame.find("type");
    if (typeIt == frame.end() || !typeIt->is_string()) return decision;
    const std::string type = typeIt->get<std::string>();

    if (type == "welcome") {
        decision.kind = DownKind::kLog;
        decision.message = "hub accepted gateway " +
                           (frame.contains("gatewayId") && frame.at("gatewayId").is_string()
                                ? frame.at("gatewayId").get<std::string>()
                                : std::string());
        return decision;
    }
    if (type == "reject") {
        decision.kind = DownKind::kCloseRejected;
        decision.closeCode = 4003;
        decision.message =
            "hub rejected the gateway: " +
            (frame.contains("code") && frame.at("code").is_string()
                 ? frame.at("code").get<std::string>()
                 : std::string()) +
            " " +
            (frame.contains("message") && frame.at("message").is_string()
                 ? frame.at("message").get<std::string>()
                 : std::string());
        return decision;
    }
    if (type == "ping") {
        decision.kind = DownKind::kSendPong;
        return decision;
    }
    if (type == "request") {
        decision.kind = DownKind::kForwardRequest;
        if (frame.contains("id") && frame.at("id").is_string()) {
            decision.id = frame.at("id").get<std::string>();
        }
        if (frame.contains("method") && frame.at("method").is_string()) {
            decision.method = frame.at("method").get<std::string>();
        }
        decision.hasParams = frame.contains("params") && !frame.at("params").is_null();
        if (decision.hasParams) decision.params = frame.at("params");
        decision.hasTimeout = frame.contains("timeoutMs") && frame.at("timeoutMs").is_number();
        if (decision.hasTimeout) {
            decision.timeoutMs = frame.at("timeoutMs").get<std::uint64_t>();
        }
        return decision;
    }
    if (type == "subscribe") {
        decision.kind = DownKind::kForwardSubscribe;
        if (frame.contains("id") && frame.at("id").is_string()) {
            decision.id = frame.at("id").get<std::string>();
        }
        if (frame.contains("method") && frame.at("method").is_string()) {
            decision.method = frame.at("method").get<std::string>();
        }
        decision.hasParams = frame.contains("params") && !frame.at("params").is_null();
        if (decision.hasParams) decision.params = frame.at("params");
        return decision;
    }
    if (type == "unsubscribe") {
        decision.kind = DownKind::kForwardUnsubscribe;
        if (frame.contains("id") && frame.at("id").is_string()) {
            decision.id = frame.at("id").get<std::string>();
        }
        return decision;
    }
    return decision;  // pong and unknown types are ignored
}

std::uint64_t HubLinkCore::backoffMs(int attempt, double jitter01) {
    const double exponential = std::min<double>(
        static_cast<double>(kRetryMaxMs),
        static_cast<double>(kRetryBaseMs) * std::pow(2.0, std::min(attempt, 10)));
    const double jitter = 0.8 + std::clamp(jitter01, 0.0, 1.0) * 0.4;
    return static_cast<std::uint64_t>(std::llround(exponential * jitter));
}

}  // namespace orca::gateway
