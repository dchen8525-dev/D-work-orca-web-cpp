#include "config_store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace orca::ui {

namespace {

using nlohmann::json;

std::string trim(std::string_view value) {
    std::size_t first = 0;
    std::size_t last = value.size();
    while (first < last && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return std::string(value.substr(first, last - first));
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("无法打开配置文件");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Windows PowerShell 5.1 writes UTF-8 with a BOM; JSON.parse rejects it there
// and nlohmann::json does too, so strip it before parsing.
std::string_view stripBom(std::string_view text) {
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        return text.substr(3);
    }
    return text;
}

std::string stringField(const json& value, const char* key) {
    if (!value.is_object() || !value.contains(key)) return {};
    const auto& field = value.at(key);
    return field.is_string() ? field.get<std::string>() : std::string{};
}

void loadPairing(const json& root, OrcaPairing& out) {
    if (!root.is_object() || !root.contains("orca") || !root.at("orca").is_object()) return;
    const auto& orca = root.at("orca");
    out.endpoint = stringField(orca, "endpoint");
    out.deviceToken = stringField(orca, "deviceToken");
    out.hostPublicKeyB64 = stringField(orca, "hostPublicKeyB64");
}

}  // namespace

std::string normalizeHubUrl(std::string_view value) {
    const std::string input = trim(value);
    const auto schemeEnd = input.find("://");
    if (schemeEnd == std::string::npos) {
        throw std::runtime_error("serverUrl 无法解析: " + input);
    }
    const std::string scheme = input.substr(0, schemeEnd);
    if (scheme != "ws" && scheme != "wss") {
        throw std::runtime_error("serverUrl 必须是 ws:// 或 wss:// 地址");
    }
    const std::string rest = input.substr(schemeEnd + 3);
    const auto slash = rest.find('/');
    const std::string host = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string path = slash == std::string::npos ? std::string{} : rest.substr(slash);
    if (host.empty() || host.find_first_of(" \t\r\n") != std::string::npos) {
        throw std::runtime_error("serverUrl 缺少主机名: " + input);
    }
    // Drop trailing slashes, then decide whether /gateway is already present.
    while (!path.empty() && path.back() == '/') path.pop_back();
    if (path.empty()) {
        path = "/gateway";
    } else if (path != "/gateway") {
        path += "/gateway";
    }
    return scheme + "://" + host + path;
}

ConfigStore::ConfigStore(std::filesystem::path path) : path_(std::move(path)) {}

ConfigSnapshot ConfigStore::read() const {
    ConfigSnapshot snapshot;
    snapshot.configPath = path_.string();
    std::error_code ec;
    snapshot.exists = std::filesystem::exists(path_, ec);
    if (!snapshot.exists) return snapshot;

    try {
        snapshot.raw = readText(path_);
        const json root = json::parse(stripBom(snapshot.raw));
        snapshot.serverUrl = stringField(root, "serverUrl");
        snapshot.gatewayKey = stringField(root, "gatewayKey");
        loadPairing(root, snapshot.orca);
    } catch (const std::exception& error) {
        snapshot.error = error.what();
    }
    return snapshot;
}

std::string ConfigStore::apply(const ConfigUpdate& update) {
    if (path_.empty()) {
        throw std::runtime_error("gateway 没有用 --config 启动，配置无处落盘");
    }
    if (update.empty()) {
        throw std::runtime_error("没有填写任何要修改的内容");
    }

    // Preserve unknown keys from the existing file, matching applyConfigUpdate()
    // in apps/gateway/src/config.ts:102-113.
    json root = json::object();
    std::error_code existsError;
    if (std::filesystem::exists(path_, existsError)) {
        const std::string text = readText(path_);
        try {
            const json parsed = json::parse(stripBom(text));
            if (parsed.is_object()) root = parsed;
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("现有配置不是合法 JSON，拒绝覆盖: ") + error.what());
        }
    }

    if (!update.serverUrl.empty()) {
        root["serverUrl"] = normalizeHubUrl(update.serverUrl);
    }
    if (!update.gatewayKey.empty()) {
        root["gatewayKey"] = update.gatewayKey;
    }
    if (!update.orca.empty()) {
        json orca = json::object();
        orca["endpoint"] = update.orca.endpoint;
        orca["deviceToken"] = update.orca.deviceToken;
        orca["hostPublicKeyB64"] = update.orca.hostPublicKeyB64;
        root["orca"] = std::move(orca);
    }

    const std::filesystem::path parent = path_.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) throw std::runtime_error("无法创建配置目录: " + ec.message());
    }

    const std::filesystem::path staging = path_.string() + ".tmp";
    {
        std::ofstream out(staging, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("无法写入临时配置文件");
        out << root.dump(2) << '\n';
        if (!out) throw std::runtime_error("写入临时配置文件失败");
    }

    std::error_code ec;
    if (std::filesystem::exists(path_, ec)) {
        std::filesystem::remove(path_, ec);
        if (ec) throw std::runtime_error("无法替换旧配置: " + ec.message());
    }
    std::filesystem::rename(staging, path_, ec);
    if (ec) {
        throw std::runtime_error("无法落盘配置: " + ec.message());
    }
    return "配置已保存，gateway 将按新配置重连。";
}

}  // namespace orca::ui
