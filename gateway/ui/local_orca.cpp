#include "local_orca.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace orca::ui {

namespace {

nlohmann::json readJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not read " + path.string() +
                                 ". Start desktop Orca and pair a device first.");
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    try {
        return nlohmann::json::parse(buffer.str());
    } catch (const nlohmann::json::exception&) {
        throw std::runtime_error("Could not read " + path.string() +
                                 ". Start desktop Orca and pair a device first.");
    }
}

std::string textOrEmpty(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) {
        return {};
    }
    return object.at(key).get<std::string>();
}

std::uint64_t numberOrZero(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_number()) {
        return 0;
    }
    return object.at(key).get<std::uint64_t>();
}

// 0.0.0.0 is a bind address; loopback is where this machine reaches it.
std::string loopback(const std::string& endpoint) {
    static constexpr std::string_view kBind = "://0.0.0.0:";
    static constexpr std::string_view kLoopback = "://127.0.0.1:";
    const std::size_t at = endpoint.find(kBind);
    if (at == std::string::npos) return endpoint;
    return endpoint.substr(0, at) + std::string(kLoopback) +
           endpoint.substr(at + kBind.size());
}

}  // namespace

LocalOrcaPairing readLocalOrcaPairing(const std::filesystem::path& userDataPath) {
    const nlohmann::json runtime = readJsonFile(userDataPath / "orca-runtime.json");
    const nlohmann::json devices = readJsonFile(userDataPath / "orca-devices.json");
    const nlohmann::json keypair = readJsonFile(userDataPath / "orca-e2ee-keypair.json");

    std::string endpoint;
    if (runtime.is_object() && runtime.contains("transports") &&
        runtime.at("transports").is_array()) {
        for (const auto& transport : runtime.at("transports")) {
            if (textOrEmpty(transport, "kind") == "websocket") {
                endpoint = textOrEmpty(transport, "endpoint");
                break;
            }
        }
    }
    if (endpoint.empty()) {
        throw std::runtime_error(
            "Desktop Orca is not listening on a WebSocket transport. Is it running?");
    }

    const nlohmann::json* mobile = nullptr;
    std::uint64_t newest = 0;
    if (devices.is_array()) {
        for (const auto& device : devices) {
            if (textOrEmpty(device, "scope") != "mobile") continue;
            const std::uint64_t seen = numberOrZero(device, "lastSeenAt");
            if (mobile == nullptr || seen > newest) {
                mobile = &device;
                newest = seen;
            }
        }
    }
    if (mobile == nullptr) {
        throw std::runtime_error(
            "No mobile-scope paired device in orca-devices.json; pair a phone once first.");
    }

    const std::string deviceToken = textOrEmpty(*mobile, "token");
    const std::string hostPublicKeyB64 = textOrEmpty(keypair, "publicKeyB64");
    if (deviceToken.empty() || hostPublicKeyB64.empty()) {
        throw std::runtime_error(
            "The local Orca pairing material is incomplete; pair a device again.");
    }

    LocalOrcaPairing out;
    out.pairing.endpoint = loopback(endpoint);
    out.pairing.deviceToken = deviceToken;
    out.pairing.hostPublicKeyB64 = hostPublicKeyB64;
    out.deviceId = textOrEmpty(*mobile, "deviceId");
    out.deviceName = textOrEmpty(*mobile, "name");
    if (out.deviceName.empty()) out.deviceName = out.deviceId;
    return out;
}

}  // namespace orca::ui
