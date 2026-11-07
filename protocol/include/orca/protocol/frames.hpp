#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace orca::protocol {

// One JSON text frame per message. The hub is a forwarder, not a translator:
// method / params / result / error are opaque JSON values here.
enum class FrameKind {
    kUnknown,
    kRequest,
    kSubscribe,
    kUnsubscribe,
    kPing,
    kWelcome,
    kRequestResult,
    kStreamFrame,
    kStreamError,
    kStreamReset,
    kGatewayState,
    kPong,
    kHello,
    kOrcaState,
    kReject,
};

// Strict envelope guard: must be a JSON object with a string "type".
// Mirrors isWebFrame/isHubFrame/isGatewayFrame in src/shared/*.ts.
std::optional<nlohmann::json> parseFrame(std::string_view text);
FrameKind kindOf(const nlohmann::json& frame);
std::string_view kindName(FrameKind kind);

bool isWebToHub(FrameKind kind);
bool isHubToWeb(FrameKind kind);
bool isGatewayToHub(FrameKind kind);
bool isHubToGateway(FrameKind kind);

std::optional<std::string> frameId(const nlohmann::json& frame);
std::optional<std::string> frameMethod(const nlohmann::json& frame);
bool hasParams(const nlohmann::json& frame);

// Frequently used frame constructors. Everything else is built directly as
// nlohmann::json so P3/P4 can add fields without changing this API.
nlohmann::json makeClientWelcome(bool gatewayConnected, const std::string& orcaState);
nlohmann::json makeGatewayWelcome(const std::string& gatewayId);
nlohmann::json makeHello(const std::string& key, const std::string& gatewayId,
                         const std::string& orcaState);
nlohmann::json makeRequest(const std::string& id, const std::string& method,
                           const nlohmann::json& params,
                           std::optional<std::uint32_t> timeoutMs = std::nullopt);
nlohmann::json makeRequestResult(const std::string& id, const nlohmann::json& result);
nlohmann::json makeRequestError(const std::string& id, const std::string& code,
                                const std::string& message);
nlohmann::json makeStreamReset(const std::string& id);
nlohmann::json makePing();
nlohmann::json makePong();

}  // namespace orca::protocol
