#pragma once

#include <filesystem>
#include <string>

namespace orca::ui {

// Field-for-field counterpart of AdminStatus in
// apps/gateway/src/local-admin-server.ts:7-22. Membership/version fields that
// only exist once the hub link is live are filled by the P4 runtime later.
struct AdminStatus {
    std::string gatewayId;
    std::string configPath;
    std::string pairedVia;
    std::string hubUrl;
    std::string hubKey;
    bool hubConnected = false;
    std::string orcaState = "disconnected";
    std::string orcaAppVersion;
    std::string orcaMachineName;
    std::string configNote;
    std::string version;
};

enum class VerdictLevel { kOk, kWarn, kError };

struct Verdict {
    VerdictLevel level = VerdictLevel::kError;
    std::string text;
};

enum class ChipLevel { kOn, kOff, kWarn };

struct PairingState {
    std::string chip;
    ChipLevel chipLevel = ChipLevel::kOff;
    std::string note;
};

// Four-quadrant "do I need to touch anything?" judgement, copied from the old
// page's render() at local-admin-server.ts:369-388.
Verdict evaluate(const AdminStatus& status);
PairingState pairingState(const AdminStatus& status);
std::string orcaIdentity(const AdminStatus& status);

// P4 will replace this with a provider backed by hub_link / orca_bridge. The
// UI only ever consumes AdminStatus, so the window code does not change.
class StatusProvider {
public:
    virtual ~StatusProvider() = default;
    virtual AdminStatus snapshot() const = 0;
};

// Skeleton provider: reads the real config file, reports both connections as
// down (nothing is connected yet) and marks its own limitations in configNote.
// All values shown in the window are therefore traceable, not random mock data.
class LocalStatusProvider final : public StatusProvider {
public:
    explicit LocalStatusProvider(std::filesystem::path configPath);
    AdminStatus snapshot() const override;

private:
    std::filesystem::path configPath_;
};

}  // namespace orca::ui
