#include "pairing.hpp"

#include <orca/crypto/base64.hpp>
#include <orca/crypto/bytes.hpp>

#include <nlohmann/json.hpp>

#include <cctype>
#include <stdexcept>

namespace orca::ui {

namespace {

std::string trim(std::string_view value) {
    std::size_t first = 0;
    std::size_t last = value.size();
    while (first < last && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return std::string(value.substr(first, last - first));
}

bool isCodeChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

std::string requiredString(const nlohmann::json& root, const char* key, const char* message) {
    if (!root.is_object() || !root.contains(key)) {
        throw std::runtime_error(message);
    }
    const auto& field = root.at(key);
    if (!field.is_string() || field.get<std::string>().empty()) {
        throw std::runtime_error(message);
    }
    return field.get<std::string>();
}

}  // namespace

PairingOffer decodePairingCode(std::string_view input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        throw std::runtime_error("配对码为空");
    }

    // The separator must be "?code=" or "&code=". Matching a bare "code="
    // anywhere would let those five chars appear inside a base64 payload and
    // cut it mid-way.
    std::size_t start = 0;
    bool foundSeparator = false;
    for (std::size_t i = 0; i + 5 < trimmed.size(); ++i) {
        if ((trimmed[i] == '?' || trimmed[i] == '&') && trimmed.compare(i + 1, 5, "code=") == 0) {
            start = i + 6;
            foundSeparator = true;
            break;
        }
    }
    if (!foundSeparator) {
        if (trimmed.compare(0, 5, "code=") == 0) {
            start = 5;
        } else {
            start = 0;
        }
    }

    std::size_t codeLength = 0;
    while (start + codeLength < trimmed.size() && isCodeChar(trimmed[start + codeLength])) {
        ++codeLength;
    }
    if (codeLength == 0) {
        throw std::runtime_error("配对码里没有可解码的 base64 内容");
    }

    std::string base64(trimmed.substr(start, codeLength));
    for (char& c : base64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (base64.size() % 4 != 0) base64.push_back('=');

    orca::crypto::Bytes decoded;
    if (!orca::crypto::base64Decode(base64, decoded)) {
        throw std::runtime_error("配对码无法解码: base64 内容不合法");
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(
            std::string(reinterpret_cast<const char*>(decoded.data()), decoded.size()));
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("配对码无法解码: ") + error.what());
    }
    if (!root.is_object()) {
        throw std::runtime_error("配对码无法解码: 内容不是 JSON 对象");
    }

    PairingOffer offer;
    offer.deviceToken = requiredString(root, "deviceToken", "配对码缺少 deviceToken");
    offer.publicKeyB64 = requiredString(root, "publicKeyB64", "配对码缺少 publicKeyB64");
    if (root.contains("scope") && root.at("scope").is_string()) {
        offer.scope = root.at("scope").get<std::string>();
        if (offer.scope != "mobile") {
            throw std::runtime_error("配对码 scope 是 " + offer.scope + "，需要 mobile");
        }
    }
    if (root.contains("v") && root.at("v").is_number_integer()) {
        offer.version = root.at("v").get<int>();
    }
    if (root.contains("endpoint") && root.at("endpoint").is_string()) {
        offer.endpoint = root.at("endpoint").get<std::string>();
    }
    if (root.contains("pairedDeviceId") && root.at("pairedDeviceId").is_string()) {
        offer.pairedDeviceId = root.at("pairedDeviceId").get<std::string>();
    }
    return offer;
}

std::string loopbackEndpoint(std::string_view endpoint) {
    const std::string value = trim(endpoint);
    const auto schemeEnd = value.find("://");
    if (schemeEnd == std::string::npos) {
        throw std::runtime_error("配对码 endpoint 无法解析: " + value);
    }
    const std::string rest = value.substr(schemeEnd + 3);
    const auto slash = rest.find('/');
    const std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string path = slash == std::string::npos ? std::string{} : rest.substr(slash);
    const auto colon = authority.rfind(':');
    const std::string port = colon == std::string::npos ? std::string{} : authority.substr(colon + 1);
    if (port.empty() || port.find_first_not_of("0123456789") != std::string::npos) {
        throw std::runtime_error("配对码 endpoint 无法解析: " + value);
    }
    if (path == "/") path.clear();
    return "ws://127.0.0.1:" + port + path;
}

}  // namespace orca::ui
