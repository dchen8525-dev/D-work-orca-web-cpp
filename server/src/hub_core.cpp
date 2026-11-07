#include <orca/server/hub_core.hpp>

#include <orca/crypto/bytes.hpp>
#include <orca/crypto/sha256.hpp>
#include <orca/protocol/versions.hpp>

#include <algorithm>

namespace orca::server {

namespace {

using nlohmann::json;

bool isRecord(const json& value) {
    return value.is_object();
}

std::optional<std::string> stringField(const json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) {
        return std::nullopt;
    }
    return object.at(key).get<std::string>();
}

HubAction sendAction(std::uint64_t connectionId, const json& frame) {
    HubAction action;
    action.kind = HubAction::Kind::kSend;
    action.connectionId = connectionId;
    action.json = frame.dump();
    return action;
}

HubAction closeAction(std::uint64_t connectionId, std::uint16_t code, std::string reason) {
    HubAction action;
    action.kind = HubAction::Kind::kClose;
    action.connectionId = connectionId;
    action.closeCode = code;
    action.closeReason = std::move(reason);
    return action;
}

json linkedFrame(const char* type, const std::string& id, bool ok, const json& payload) {
    json frame{{"type", type}, {"id", id}, {"ok", ok}};
    if (ok) {
        frame["result"] = payload;
    } else {
        frame["error"] = payload;
    }
    return frame;
}

}  // namespace

HubCore::HubCore(std::string gatewayKey, std::function<void(const std::string&)> log)
    : gatewayKey_(std::move(gatewayKey)), log_(std::move(log)) {}

nlohmann::json HubCore::getGatewayState() const {
    json state{{"connected", gatewayAdmitted_}, {"orcaState", orcaState_}};
    if (!descriptor_.is_null()) state["orca"] = descriptor_;
    return state;
}

bool HubCore::isAdmittedGateway(std::uint64_t connectionId) const noexcept {
    return gatewayAdmitted_ && connectionId == gatewayConnectionId_;
}

std::vector<HubAction> HubCore::onGatewayAttached(std::uint64_t /*connectionId*/) {
    return {};
}

std::vector<HubAction> HubCore::helloTimeoutActions(std::uint64_t connectionId) const {
    json reject{{"type", "reject"}, {"code", "hello_timeout"}, {"message", "No hello frame"}};
    return {sendAction(connectionId, reject),
            closeAction(connectionId, 4001, "hello timeout")};
}

std::vector<HubAction> HubCore::onGatewayFrame(std::uint64_t connectionId,
                                               const json& frame) {
    if (!isRecord(frame)) return {};
    const auto type = stringField(frame, "type");
    if (!type) return {};

    if (!isAdmittedGateway(connectionId)) {
        if (*type != "hello") {
            return {closeAction(connectionId, 4001, "hello required")};
        }
        if (!verifyKey(frame.contains("key") ? frame.at("key") : json(nullptr))) {
            if (log_) log_("gateway rejected: bad key");
            json reject{{"type", "reject"}, {"code", "unauthorized"}, {"message", "Bad gateway key"}};
            return {sendAction(connectionId, reject),
                    closeAction(connectionId, 4003, "unauthorized")};
        }
        return admitGateway(connectionId, frame);
    }

    std::vector<HubAction> actions;
    if (*type == "request-result") {
        const auto id = stringField(frame, "id");
        if (!id) return {};
        const auto entry = pending_.find(*id);
        if (entry == pending_.end()) return {};
        const Pending pending = entry->second;
        pending_.erase(entry);
        const bool ok = frame.contains("ok") && frame.at("ok").is_boolean() &&
                        frame.at("ok").get<bool>();
        const json payload =
            ok ? (frame.contains("result") ? frame.at("result") : json(nullptr))
               : (frame.contains("error") ? frame.at("error")
                                          : json{{"code", "unknown"}, {"message", "missing error"}});
        actions.push_back(sendAction(pending.clientConnectionId,
                                     linkedFrame("request-result", pending.localId, ok, payload)));
        return actions;
    }
    if (*type == "stream-frame" || *type == "stream-error" || *type == "stream-reset") {
        const auto id = stringField(frame, "id");
        if (!id) return {};
        const auto entry = subscriptions_.find(*id);
        if (entry == subscriptions_.end()) return {};
        const Subscription& subscription = entry->second;
        if (*type == "stream-frame") {
            actions.push_back(sendAction(
                subscription.clientConnectionId,
                json{{"type", "stream-frame"},
                     {"id", subscription.localId},
                     {"result", frame.contains("result") ? frame.at("result") : json(nullptr)}}));
        } else if (*type == "stream-error") {
            actions.push_back(sendAction(
                subscription.clientConnectionId,
                json{{"type", "stream-error"},
                     {"id", subscription.localId},
                     {"error", frame.contains("error") ? frame.at("error")
                                                       : json{{"code", "unknown"},
                                                              {"message", "missing error"}}}}));
        } else {
            actions.push_back(sendAction(subscription.clientConnectionId,
                                         json{{"type", "stream-reset"}, {"id", subscription.localId}}));
        }
        return actions;
    }
    if (*type == "orca-state") {
        orcaState_ = frame.contains("state") ? frame.at("state") : json(nullptr);
        std::optional<std::string> message;
        if (frame.contains("message") && frame.at("message").is_string()) {
            message = frame.at("message").get<std::string>();
        }
        return broadcastState(true, message);
    }
    if (*type == "ping") {
        // The gateway probes for liveness; silence reads as a dead hub after 75s.
        actions.push_back(sendAction(connectionId, json{{"type", "pong"}}));
        return actions;
    }
    // "pong" and a duplicate "hello" are ignored, exactly like the TS switch.
    return actions;
}

std::vector<HubAction> HubCore::onGatewayDropped(std::uint64_t connectionId) {
    if (!isAdmittedGateway(connectionId)) return {};
    gatewayAdmitted_ = false;
    gatewayConnectionId_ = 0;
    gatewayId_.clear();
    orcaState_ = nullptr;
    descriptor_ = nullptr;
    if (log_) log_("gateway disconnected");
    std::vector<HubAction> actions = failPending("remote_runtime_unavailable",
                                                 "Home gateway is not connected");
    std::vector<HubAction> broadcast = broadcastState(false, std::nullopt);
    actions.insert(actions.end(), broadcast.begin(), broadcast.end());
    return actions;
}

std::vector<HubAction> HubCore::onClientAttached(std::uint64_t connectionId) {
    connectedClients_[connectionId] = true;
    json welcome{{"type", "welcome"},
                 {"v", orca::protocol::kWebProtocolVersion},
                 {"gatewayConnected", gatewayAdmitted_},
                 {"orcaState", orcaState_}};
    if (!descriptor_.is_null()) welcome["orca"] = descriptor_;
    return {sendAction(connectionId, welcome)};
}

std::vector<HubAction> HubCore::onClientFrame(std::uint64_t connectionId, const json& frame) {
    if (!isRecord(frame)) return {};
    const auto type = stringField(frame, "type");
    if (!type) return {};

    if (*type == "ping") {
        return {sendAction(connectionId, json{{"type", "pong"}})};
    }
    if (*type == "request") {
        const auto id = stringField(frame, "id");
        const auto method = stringField(frame, "method");
        if (!id || !method) return {};
        if (!gatewayAdmitted_) {
            return {sendAction(connectionId,
                               linkedFrame("request-result", *id, false,
                                           json{{"code", "remote_runtime_unavailable"},
                                                {"message", "Home gateway is not connected"}}))};
        }
        const std::string gatewayId = newGatewayId(connectionId);
        Pending pending{connectionId, *id};
        pending_.emplace(gatewayId, pending);
        json forward{{"type", "request"}, {"id", gatewayId}, {"method", *method}};
        if (frame.contains("params") && !frame.at("params").is_null()) {
            // TS: ...(frame.params === undefined ? {} : { params }) — the key
            // must be omitted when the client omitted it.
            forward["params"] = frame.at("params");
        }
        if (frame.contains("timeoutMs") && frame.at("timeoutMs").is_number()) {
            forward["timeoutMs"] = frame.at("timeoutMs");
        }
        return {sendAction(gatewayConnectionId_, forward)};
    }
    if (*type == "subscribe") {
        const auto id = stringField(frame, "id");
        const auto method = stringField(frame, "method");
        if (!id || !method) return {};
        const std::string gatewayId = newGatewayId(connectionId);

        ClientSubscription& clientSubscription = clientSubscriptions_[connectionId][*id];
        clientSubscription.gatewayId = gatewayId;
        clientSubscription.method = *method;
        clientSubscription.hasParams = frame.contains("params") && !frame.at("params").is_null();
        clientSubscription.params =
            clientSubscription.hasParams ? frame.at("params") : json(nullptr);

        Subscription& subscription = subscriptions_[gatewayId];
        subscription.clientConnectionId = connectionId;
        subscription.localId = *id;
        subscription.method = *method;
        subscription.params = clientSubscription.params;
        subscription.hasParams = clientSubscription.hasParams;

        std::vector<HubAction> actions;
        if (gatewayAdmitted_) {
            json forward{{"type", "subscribe"}, {"id", gatewayId}, {"method", *method}};
            if (clientSubscription.hasParams) forward["params"] = clientSubscription.params;
            actions.push_back(sendAction(gatewayConnectionId_, forward));
        }
        return actions;
    }
    if (*type == "unsubscribe") {
        const auto id = stringField(frame, "id");
        if (!id) return {};
        auto& clientSubs = clientSubscriptions_[connectionId];
        const auto it = clientSubs.find(*id);
        if (it == clientSubs.end()) return {};
        const std::string gatewayId = it->second.gatewayId;
        clientSubs.erase(it);
        subscriptions_.erase(gatewayId);
        if (gatewayAdmitted_) {
            return {sendAction(gatewayConnectionId_,
                               json{{"type", "unsubscribe"}, {"id", gatewayId}})};
        }
        return {};
    }
    return {};
}

std::vector<HubAction> HubCore::onClientDropped(std::uint64_t connectionId) {
    if (connectedClients_.erase(connectionId) == 0) return {};
    std::vector<HubAction> actions;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->second.clientConnectionId == connectionId) {
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    const auto clientSubs = clientSubscriptions_.find(connectionId);
    if (clientSubs != clientSubscriptions_.end()) {
        for (const auto& [localId, subscription] : clientSubs->second) {
            subscriptions_.erase(subscription.gatewayId);
            if (gatewayAdmitted_) {
                actions.push_back(sendAction(gatewayConnectionId_,
                                             json{{"type", "unsubscribe"},
                                                  {"id", subscription.gatewayId}}));
            }
        }
        clientSubscriptions_.erase(clientSubs);
    }
    return actions;
}

std::vector<HubAction> HubCore::shutdownActions() const {
    std::vector<HubAction> actions;
    for (const auto& [connectionId, present] : connectedClients_) {
        (void)present;
        actions.push_back(closeAction(connectionId, 1001, "server shutting down"));
    }
    if (gatewayAdmitted_) {
        actions.push_back(closeAction(gatewayConnectionId_, 1001, "server shutting down"));
    }
    return actions;
}

std::string HubCore::newGatewayId(std::uint64_t clientConnectionId) {
    // client.nextId in the TS starts at 1 per connection; a shared counter is
    // fine because ids are only compared for equality, never parsed.
    return "client" + std::to_string(clientConnectionId) + "-" + std::to_string(nextGatewaySeq_++);
}

bool HubCore::verifyKey(const json& key) const {
    if (!key.is_string()) return false;
    const std::string provided = key.get<std::string>();
    const auto expected = orca::crypto::sha256(gatewayKey_);
    const auto actual = orca::crypto::sha256(provided);
    return orca::crypto::constantTimeEqual(
        orca::crypto::Bytes(expected.begin(), expected.end()),
        orca::crypto::Bytes(actual.begin(), actual.end()));
}

std::vector<HubAction> HubCore::admitGateway(std::uint64_t connectionId, const json& hello) {
    std::vector<HubAction> actions;
    if (gatewayAdmitted_) {
        if (log_) log_("replacing the previous gateway connection");
        const std::uint64_t previous = gatewayConnectionId_;
        actions.push_back(closeAction(previous, 4009, "replaced by a new gateway connection"));
        gatewayAdmitted_ = false;
    }
    gatewayAdmitted_ = true;
    gatewayConnectionId_ = connectionId;
    gatewayId_ = stringField(hello, "gatewayId").value_or("");
    orcaState_ = hello.contains("orcaState") ? hello.at("orcaState") : json(nullptr);
    descriptor_ = hello.contains("orca") ? hello.at("orca") : json(nullptr);
    if (log_) {
        log_("gateway " + gatewayId_ + " connected (orca=" +
             (orcaState_.is_string() ? orcaState_.get<std::string>() : "null") + ")");
    }
    actions.push_back(sendAction(connectionId, json{{"type", "welcome"}, {"gatewayId", gatewayId_}}));

    // Requests parked on the previous socket will never answer; fail them now
    // instead of letting every browser wait out its own 30s timeout.
    std::vector<HubAction> failures =
        failPending("remote_runtime_unavailable", "Home gateway reconnected");
    actions.insert(actions.end(), failures.begin(), failures.end());
    std::vector<HubAction> broadcast = broadcastState(true, std::nullopt);
    actions.insert(actions.end(), broadcast.begin(), broadcast.end());

    // The hub owns subscription ids; replay them so a reconnect resumes every stream.
    for (const auto& [clientConnectionId, subs] : clientSubscriptions_) {
        for (const auto& [localId, subscription] : subs) {
            json forward{{"type", "subscribe"},
                         {"id", subscription.gatewayId},
                         {"method", subscription.method}};
            if (subscription.hasParams) forward["params"] = subscription.params;
            actions.push_back(sendAction(gatewayConnectionId_, forward));
            actions.push_back(sendAction(clientConnectionId,
                                         json{{"type", "stream-reset"}, {"id", localId}}));
        }
    }
    return actions;
}

std::vector<HubAction> HubCore::failPending(const std::string& code, const std::string& message) {
    std::vector<HubAction> actions;
    for (auto it = pending_.begin(); it != pending_.end();) {
        const Pending pending = it->second;
        it = pending_.erase(it);
        actions.push_back(sendAction(pending.clientConnectionId,
                                     linkedFrame("request-result", pending.localId, false,
                                                 json{{"code", code}, {"message", message}})));
    }
    return actions;
}

std::vector<HubAction> HubCore::broadcastState(bool connected,
                                               const std::optional<std::string>& message) const {
    std::vector<HubAction> actions;
    for (const auto& [connectionId, present] : connectedClients_) {
        (void)present;
        json frame{{"type", "gateway-state"}, {"connected", connected}, {"orcaState", orcaState_}};
        if (message) frame["message"] = *message;
        actions.push_back(sendAction(connectionId, frame));
    }
    return actions;
}

}  // namespace orca::server
