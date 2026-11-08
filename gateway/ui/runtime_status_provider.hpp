#pragma once

#include <orca/gateway/net/hub_client.hpp>
#include "ui/config_store.hpp"
#include "ui/status_model.hpp"

#include <memory>

namespace orca::ui {

// P4 runtime status: the real hub connection state from the worker-thread
// HubClient, layered over the config file. The Orca side reports whatever the
// bridge says, which is "disconnected" for the P4-a null bridge.
class RuntimeStatusProvider final : public StatusProvider {
public:
    RuntimeStatusProvider(std::filesystem::path configPath,
                          std::shared_ptr<gateway::HubClient> hubClient)
        : configPath_(std::move(configPath)), hubClient_(std::move(hubClient)) {}

    AdminStatus snapshot() const override {
        const ConfigStore store(configPath_);
        const ConfigSnapshot file = store.read();
        const gateway::HubClient::Snapshot hub = hubClient_->snapshot();

        AdminStatus status;
        status.gatewayId = file.gatewayId;
        status.configPath = configPath_.string();
        status.hubUrl = file.serverUrl;
        status.hubKey = file.gatewayKey;
        status.version = orca::buildVersion();
        status.pairedVia = file.orca.deviceToken.empty() ? std::string{} : std::string("local-orca");
        status.hubConnected = hub.hubConnected;
        // The null bridge has no state changes; P2's real bridge drives this.
        status.orcaState = "disconnected";
        status.configNote = file.error.empty()
            ? (hub.hubConnected
                   ? std::string{}
                   : std::string("hub 未连接") +
                         (hub.lastError.empty() ? std::string{} : "：" + hub.lastError))
            : file.error;
        return status;
    }

private:
    std::filesystem::path configPath_;
    std::shared_ptr<gateway::HubClient> hubClient_;
};

}  // namespace orca::ui
