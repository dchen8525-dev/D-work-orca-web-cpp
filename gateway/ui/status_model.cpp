#include "status_model.hpp"

#include "config_store.hpp"

#include <orca/version.hpp>

namespace orca::ui {

Verdict evaluate(const AdminStatus& status) {
    const bool orcaLive = status.orcaState == "connected";
    if (status.hubConnected && orcaLive) {
        return {VerdictLevel::kOk, "一切正常：hub 连通，Orca 配对在用。以下设置无需改动。"};
    }
    if (status.hubConnected) {
        return {VerdictLevel::kWarn,
                "hub 已连通；桌面 Orca 未连接。先确认 Orca 在运行，仍无果再重新配对。"};
    }
    if (orcaLive) {
        return {VerdictLevel::kWarn,
                "桌面 Orca 在线；hub 未连通。先检查 VPS 是否在运行，换 hub 时用下方的修改入口。"};
    }
    return {VerdictLevel::kError,
            "两组连接均未建立。初次安装属此情形：先填写 hub 地址与 key，再完成配对。"};
}

std::string orcaIdentity(const AdminStatus& status) {
    std::string identity;
    if (!status.orcaAppVersion.empty()) identity = "Orca " + status.orcaAppVersion;
    if (!status.orcaMachineName.empty()) {
        if (!identity.empty()) identity += " · ";
        identity += status.orcaMachineName;
    }
    return identity;
}

PairingState pairingState(const AdminStatus& status) {
    const std::string identity = orcaIdentity(status);
    if (status.orcaState == "connected") {
        return {"在线", ChipLevel::kOn,
                status.orcaMachineName.empty()
                    ? "已配对，正在通信"
                    : "已配对，正在和 " + status.orcaMachineName + " 上的 Orca 通信"};
    }
    if (!identity.empty()) {
        return {"离线", ChipLevel::kWarn, "已配对，但 Orca 现在没连上——桌面 Orca 开机/打开了吗？"};
    }
    if (status.pairedVia == "local-orca") {
        return {"等待", ChipLevel::kWarn, "本轮用桌面 Orca 的本地配对材料读取，还未走通连接"};
    }
    return {"未配对", ChipLevel::kOff, "还没有配对材料"};
}

LocalStatusProvider::LocalStatusProvider(std::filesystem::path configPath)
    : configPath_(std::move(configPath)) {}

AdminStatus LocalStatusProvider::snapshot() const {
    const ConfigStore store(configPath_);
    const ConfigSnapshot file = store.read();

    AdminStatus status;
    status.gatewayId = "local-gateway";   // P4: runtime-generated gateway id
    status.configPath = configPath_.string();
    status.hubUrl = file.serverUrl;
    status.hubKey = file.gatewayKey;
    status.version = orca::buildVersion();
    // The old page used pairedVia="local-orca" for "pairing material was read
    // locally but the connection has not been established yet" -- exactly the
    // skeleton's state when gateway.json already has an orca block.
    status.pairedVia = file.orca.deviceToken.empty() ? std::string{} : std::string("local-orca");
    status.hubConnected = false;          // P4: hub_link::isConnected()
    status.orcaState = "disconnected";    // P4: orca_bridge::getState()
    status.configNote = file.error.empty()
        ? std::string("状态页当前读取本地配置；hub / Orca 连接状态在 P4 接入。")
        : file.error;
    return status;
}

}  // namespace orca::ui
