#include <orca/protocol/capabilities.hpp>

#include <utility>

namespace orca::protocol {

nlohmann::json makeClientCapabilitiesParams() {
    nlohmann::json list = nlohmann::json::array();
    for (const auto capability : kClientCapabilities) {
        list.push_back(std::string(capability));
    }
    return nlohmann::json{{"clientCapabilities", std::move(list)}};
}

nlohmann::json makeClientCapabilitiesRequest(const std::string& id,
                                             const std::string& deviceToken) {
    return nlohmann::json{
        {"id", id},
        {"deviceToken", deviceToken},
        {"method", kClientCapabilitiesMethod},
        {"params", makeClientCapabilitiesParams()},
    };
}

}  // namespace orca::protocol
