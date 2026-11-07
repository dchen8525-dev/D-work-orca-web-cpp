#include <orca/protocol/frames.hpp>

#include <orca/protocol/versions.hpp>

namespace orca::protocol {

namespace {

struct KindEntry {
    std::string_view name;
    FrameKind kind;
};

constexpr KindEntry kKinds[] = {
    {"request", FrameKind::kRequest},
    {"subscribe", FrameKind::kSubscribe},
    {"unsubscribe", FrameKind::kUnsubscribe},
    {"ping", FrameKind::kPing},
    {"welcome", FrameKind::kWelcome},
    {"request-result", FrameKind::kRequestResult},
    {"stream-frame", FrameKind::kStreamFrame},
    {"stream-error", FrameKind::kStreamError},
    {"stream-reset", FrameKind::kStreamReset},
    {"gateway-state", FrameKind::kGatewayState},
    {"pong", FrameKind::kPong},
    {"hello", FrameKind::kHello},
    {"orca-state", FrameKind::kOrcaState},
    {"reject", FrameKind::kReject},
};

}  // namespace

std::optional<nlohmann::json> parseFrame(std::string_view text) {
    auto parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return std::nullopt;
    const auto type = parsed.find("type");
    if (type == parsed.end() || !type->is_string()) return std::nullopt;
    return parsed;
}

FrameKind kindOf(const nlohmann::json& frame) {
    const auto type = frame.find("type");
    if (type == frame.end() || !type->is_string()) return FrameKind::kUnknown;
    const std::string name = type->get<std::string>();
    for (const auto& entry : kKinds) {
        if (name == entry.name) return entry.kind;
    }
    return FrameKind::kUnknown;
}

std::string_view kindName(FrameKind kind) {
    for (const auto& entry : kKinds) {
        if (entry.kind == kind) return entry.name;
    }
    return "unknown";
}

bool isWebToHub(FrameKind kind) {
    return kind == FrameKind::kRequest || kind == FrameKind::kSubscribe ||
           kind == FrameKind::kUnsubscribe || kind == FrameKind::kPing;
}

bool isHubToWeb(FrameKind kind) {
    return kind == FrameKind::kWelcome || kind == FrameKind::kRequestResult ||
           kind == FrameKind::kStreamFrame || kind == FrameKind::kStreamError ||
           kind == FrameKind::kStreamReset || kind == FrameKind::kGatewayState ||
           kind == FrameKind::kPong;
}

bool isGatewayToHub(FrameKind kind) {
    return kind == FrameKind::kHello || kind == FrameKind::kRequestResult ||
           kind == FrameKind::kStreamFrame || kind == FrameKind::kStreamError ||
           kind == FrameKind::kStreamReset || kind == FrameKind::kOrcaState ||
           kind == FrameKind::kPing || kind == FrameKind::kPong;
}

bool isHubToGateway(FrameKind kind) {
    return kind == FrameKind::kWelcome || kind == FrameKind::kRequest ||
           kind == FrameKind::kSubscribe || kind == FrameKind::kUnsubscribe ||
           kind == FrameKind::kPing || kind == FrameKind::kPong ||
           kind == FrameKind::kReject;
}

std::optional<std::string> frameId(const nlohmann::json& frame) {
    const auto it = frame.find("id");
    if (it == frame.end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
}

std::optional<std::string> frameMethod(const nlohmann::json& frame) {
    const auto it = frame.find("method");
    if (it == frame.end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
}

bool hasParams(const nlohmann::json& frame) {
    return frame.contains("params");
}

nlohmann::json makeClientWelcome(bool gatewayConnected, const std::string& orcaState) {
    return {
        {"type", "welcome"},
        {"v", kWebProtocolVersion},
        {"gatewayConnected", gatewayConnected},
        {"orcaState", orcaState.empty() ? nlohmann::json(nullptr) : nlohmann::json(orcaState)},
    };
}

nlohmann::json makeGatewayWelcome(const std::string& gatewayId) {
    return {{"type", "welcome"}, {"gatewayId", gatewayId}};
}

nlohmann::json makeHello(const std::string& key, const std::string& gatewayId,
                         const std::string& orcaState) {
    return {
        {"type", "hello"},
        {"v", kLinkProtocolVersion},
        {"key", key},
        {"gatewayId", gatewayId},
        {"orcaState", orcaState},
    };
}

nlohmann::json makeRequest(const std::string& id, const std::string& method,
                           const nlohmann::json& params,
                           std::optional<std::uint32_t> timeoutMs) {
    nlohmann::json frame = {{"type", "request"}, {"id", id}, {"method", method}};
    if (!params.is_null()) frame["params"] = params;
    if (timeoutMs) frame["timeoutMs"] = *timeoutMs;
    return frame;
}

nlohmann::json makeRequestResult(const std::string& id, const nlohmann::json& result) {
    return {{"type", "request-result"}, {"id", id}, {"ok", true}, {"result", result}};
}

nlohmann::json makeRequestError(const std::string& id, const std::string& code,
                                const std::string& message) {
    return {
        {"type", "request-result"},
        {"id", id},
        {"ok", false},
        {"error", {{"code", code}, {"message", message}}},
    };
}

nlohmann::json makeStreamReset(const std::string& id) {
    return {{"type", "stream-reset"}, {"id", id}};
}

nlohmann::json makePing() { return {{"type", "ping"}}; }
nlohmann::json makePong() { return {{"type", "pong"}}; }

}  // namespace orca::protocol
