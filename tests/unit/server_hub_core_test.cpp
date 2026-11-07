// P3: the pure hub routing state machine (hub_core) — gateway admission,
// request/subscription id mapping, reconnect replay, and pending failure.
// These behaviours route production traffic; each mirrors hub.ts exactly.
//
// Connection ids are minted from the core (both gateways and browsers); the
// admitted gateway is the last connection whose hello key verified.

#include <orca/server/hub_core.hpp>

#include "test_util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>

using orca::server::HubAction;
using orca::server::HubCore;

namespace {

nlohmann::json getSent(const std::vector<HubAction>& actions, std::size_t index) {
    if (index >= actions.size() || actions[index].kind != HubAction::Kind::kSend) {
        return nlohmann::json::object();
    }
    return nlohmann::json::parse(actions[index].json);
}

bool hasClose(const std::vector<HubAction>& actions, std::uint64_t id, std::uint16_t code) {
    return std::any_of(actions.begin(), actions.end(), [&](const HubAction& action) {
        return action.kind == HubAction::Kind::kClose && action.connectionId == id &&
               action.closeCode == code;
    });
}

nlohmann::json findSend(const std::vector<HubAction>& actions, std::uint64_t id,
                        const std::string& type) {
    for (const HubAction& action : actions) {
        if (action.kind != HubAction::Kind::kSend || action.connectionId != id) continue;
        const auto parsed = nlohmann::json::parse(action.json);
        if (parsed.value("type", "") == type) return parsed;
    }
    return nlohmann::json::object();
}

}  // namespace

int main() {
    HubCore hub("test-gateway-key", [](const std::string&) {});
    const std::uint64_t gateway = hub.nextConnectionId();
    hub.onGatewayAttached(gateway);

    // --- gateway admission -------------------------------------------------
    std::vector<HubAction> actions = hub.onGatewayFrame(gateway, {
        {"type", "hello"}, {"v", 1}, {"key", "wrong key"},
        {"gatewayId", "gw1"}, {"orcaState", "connected"}});
    test::check(!hub.isAdmittedGateway(gateway),
                "a bad key does not admit the gateway");
    test::check(!getSent(actions, 0).empty() && getSent(actions, 0).value("type", "") == "reject" &&
                    getSent(actions, 0).value("code", "") == "unauthorized",
                "a bad key answers with reject/unauthorized");
    test::check(hasClose(actions, gateway, 4003), "a bad key closes with 4003");

    const nlohmann::json hello{
        {"type", "hello"}, {"v", 1}, {"key", "test-gateway-key"},
        {"gatewayId", "gw1"}, {"orcaState", "connected"},
        {"orca", {{"appVersion", "1.2.3"}}}};
    actions = hub.onGatewayFrame(gateway, hello);
    test::check(hub.isAdmittedGateway(gateway), "a correct key admits");
    test::check(getSent(actions, 0).value("type", "") == "welcome" &&
                    getSent(actions, 0).value("gatewayId", "") == "gw1",
                "admission answers welcome with the gateway id");
    test::check(hub.getGatewayState().value("connected", false) &&
                    hub.getGatewayState().value("orcaState", "") == "connected" &&
                    hub.getGatewayState().contains("orca"),
                "gateway state exposes connection, orcaState and the descriptor");

    // A second socket that speaks before its hello is closed with 4001.
    const std::uint64_t gateway2 = hub.nextConnectionId();
    actions = hub.onGatewayFrame(gateway2, {{"type", "ping"}});
    test::check(hasClose(actions, gateway2, 4001), "a second socket without hello gets 4001");

    // --- client lifecycle --------------------------------------------------
    const std::uint64_t client = hub.nextConnectionId();
    actions = hub.onClientAttached(client);
    const nlohmann::json welcome = getSent(actions, 0);
    test::check(welcome.value("type", "") == "welcome" && welcome.value("v", 0) == 1 &&
                    welcome.value("gatewayConnected", true),
                "welcome carries v=1 and the gateway flag");
    test::check(welcome.contains("orca") && welcome.at("orca").value("appVersion", "") == "1.2.3",
                "welcome forwards the orca descriptor");

    // --- request mapping ---------------------------------------------------
    actions = hub.onClientFrame(client, {{"type", "request"}, {"id", "r1"},
                                         {"method", "agentSession.list"}});
    test::check(actions.size() == 1, "a request forwards exactly one frame");
    const nlohmann::json forwarded = getSent(actions, 0);
    test::check(forwarded.value("type", "") == "request" &&
                    forwarded.value("method", "") == "agentSession.list",
                "request forwards method verbatim");
    const std::string gatewayRequestId = forwarded.value("id", "");
    test::check(gatewayRequestId.rfind("client" + std::to_string(client) + "-", 0) == 0,
                "the hub owns the forwarded request id namespace");
    test::check(!forwarded.contains("params"),
                "an omitted params key stays omitted");

    actions = hub.onGatewayFrame(gateway, {
        {"type", "request-result"}, {"id", gatewayRequestId}, {"ok", true},
        {"result", {{"items", {1, 2, 3}}}}});
    const nlohmann::json result = getSent(actions, 0);
    test::check(result.value("id", "") == "r1" && result.value("ok", false) &&
                    result.at("result").at("items").size() == 3,
                "results route back under the browser's own id");

    // Unknown gateway result ids are dropped silently.
    test::check(hub.onGatewayFrame(gateway,
                                   {{"type", "request-result"}, {"id", "nope"}, {"ok", true}})
                    .empty(),
                "an unknown result id is ignored");

    // --- subscriptions and reconnect replay --------------------------------
    actions = hub.onClientFrame(client, {{"type", "subscribe"}, {"id", "s1"},
                                         {"method", "task.subscribe"}, {"params", {{"task", "t9"}}}});
    test::check(!findSend(actions, gateway, "subscribe").empty(),
                "a subscribe forwards to the gateway");
    const std::string subscriptionId =
        findSend(actions, gateway, "subscribe").value("id", "");

    actions = hub.onGatewayFrame(gateway, {
        {"type", "stream-frame"}, {"id", subscriptionId}, {"result", {{"tick", 1}}}});
    const nlohmann::json streamFrame = getSent(actions, 0);
    test::check(streamFrame.value("id", "") == "s1" &&
                    streamFrame.at("result").value("tick", 0) == 1,
                "stream frames route back under the browser id");

    // Gateway drop: state broadcasts to every client.
    actions = hub.onGatewayDropped(gateway);
    test::check(!findSend(actions, client, "gateway-state").empty() &&
                    findSend(actions, client, "gateway-state").value("connected", true) == false,
                "the drop broadcasts gateway-state(false)");

    // Reconnect: the subscription replays under its OWN hub-owned id (the TS
    // stores it once at subscribe time), plus a stream-reset so the browser
    // refetches instead of assuming continuity.
    actions = hub.onGatewayFrame(gateway, hello);
    const nlohmann::json replayed = findSend(actions, gateway, "subscribe");
    test::check(!replayed.empty() && replayed.value("method", "") == "task.subscribe" &&
                    replayed.value("id", "") == subscriptionId,
                "reconnect re-subscribes the stream under the same hub-owned id");
    test::check(!findSend(actions, client, "stream-reset").empty() &&
                    findSend(actions, client, "stream-reset").value("id", "") == "s1",
                "reconnect sends stream-reset so the browser refetches");

    // The replayed subscription still routes stream frames back.
    actions = hub.onGatewayFrame(gateway, {
        {"type", "stream-reset"}, {"id", subscriptionId}});
    test::check(!findSend(actions, client, "stream-reset").empty(),
                "frames under the replayed id still route");

    // --- replacement fails pending requests --------------------------------
    actions = hub.onClientFrame(client, {{"type", "request"}, {"id", "r2"}, {"method", "m"}});
    const std::uint64_t gateway3 = hub.nextConnectionId();
    actions = hub.onGatewayFrame(gateway3, hello);  // a NEW socket replaces the old one
    test::check(hub.isAdmittedGateway(gateway3) && !hub.isAdmittedGateway(gateway),
                "the newest hello owns the gateway slot");
    test::check(hasClose(actions, gateway, 4009), "the replaced socket is closed with 4009");
    const nlohmann::json failure = findSend(actions, client, "request-result");
    test::check(failure.value("id", "") == "r2" && failure.value("ok", true) == false &&
                    failure.at("error").value("code", "") == "remote_runtime_unavailable",
                "a replaced gateway fails pending requests immediately");

    // --- orca-state broadcast ----------------------------------------------
    actions = hub.onGatewayFrame(gateway3,
                                 {{"type", "orca-state"}, {"state", "connecting"}});
    const nlohmann::json state = findSend(actions, client, "gateway-state");
    test::check(state.value("orcaState", "") == "connecting",
                "orca-state updates broadcast to every client");

    // --- client drop unsubscribes ------------------------------------------
    actions = hub.onClientDropped(client);
    const nlohmann::json dropUnsub = findSend(actions, gateway3, "unsubscribe");
    test::check(!dropUnsub.empty() && dropUnsub.value("id", "") == subscriptionId,
                "a dropped client unsubscribes its stream");
    test::check(hub.onClientDropped(client).empty(),
                "a double drop is a no-op");

    // --- explicit unsubscribe (fresh client + subscription) ----------------
    hub.onClientAttached(client);
    hub.onClientFrame(client, {{"type", "subscribe"}, {"id", "s2"},
                               {"method", "task.subscribe"}});
    actions = hub.onClientFrame(client, {{"type", "unsubscribe"}, {"id", "s2"}});
    test::check(!findSend(actions, gateway3, "unsubscribe").empty(),
                "unsubscribe forwards to the gateway");
    test::check(hub.onClientFrame(client, {{"type", "unsubscribe"}, {"id", "s2"}}).empty(),
                "an unknown unsubscribe is ignored");

    // ping/pong on both sides.
    test::check(getSent(hub.onClientFrame(client, {{"type", "ping"}}), 0).value("type", "") ==
                    "pong",
                "client ping answers pong");
    test::check(getSent(hub.onGatewayFrame(gateway3, {{"type", "ping"}}), 0).value("type", "") ==
                    "pong",
                "gateway ping answers pong");

    // hello timeout (on a connection that never said hello) + shutdown actions.
    const std::uint64_t straggler = hub.nextConnectionId();
    test::check(hub.helloTimeoutActions(straggler).size() == 2,
                "hello timeout sends reject then closes");
    test::check(!hub.shutdownActions().empty(), "shutdown closes every connection");

    return test::finish("server_hub_core_test");
}
