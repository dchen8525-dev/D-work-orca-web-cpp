// P4: pure hub-link logic — hello frame shape, downstream frame decisions and
// the reconnect backoff, each mirrored from apps/gateway/src/hub-link.ts.

#include <orca/gateway/net/hub_link_core.hpp>

#include "test_util.hpp"

using orca::gateway::HubLinkCore;

int main() {
    HubLinkCore core("test-key", "gw-host");

    // --- hello frame --------------------------------------------------------
    const nlohmann::json hello = core.helloFrame(nlohmann::json("connected"),
                                                 nlohmann::json({{"appVersion", "1.0"}}));
    test::check(hello.value("type", "") == "hello" && hello.value("v", 0) == 1,
                "hello carries type and LINK_PROTOCOL_VERSION");
    test::check(hello.value("key", "") == "test-key" && hello.value("gatewayId", "") == "gw-host",
                "hello carries the key and gateway id");
    test::check(hello.value("orcaState", "") == "connected" &&
                    hello.at("orca").value("appVersion", "") == "1.0",
                "hello carries the orca state and descriptor");
    const nlohmann::json helloNoOrca = core.helloFrame(nlohmann::json("disconnected"),
                                                       nlohmann::json(nullptr));
    test::check(helloNoOrca.value("orcaState", "") == "disconnected" && !helloNoOrca.contains("orca"),
                "a null descriptor is omitted, not sent as null");

    // --- downstream decisions ----------------------------------------------
    const auto welcome = core.onHubFrame(nlohmann::json{{"type", "welcome"}, {"gatewayId", "gw"}});
    test::check(welcome.kind == HubLinkCore::DownKind::kLog &&
                    welcome.message.find("gw") != std::string::npos,
                "welcome is logged");

    const auto reject = core.onHubFrame(nlohmann::json{{"type", "reject"},
                                                       {"code", "unauthorized"},
                                                       {"message", "Bad gateway key"}});
    test::check(reject.kind == HubLinkCore::DownKind::kCloseRejected && reject.closeCode == 4003 &&
                    reject.message.find("unauthorized") != std::string::npos,
                "reject closes with 4003 and logs the code");

    const auto ping = core.onHubFrame(nlohmann::json{{"type", "ping"}});
    test::check(ping.kind == HubLinkCore::DownKind::kSendPong, "ping answers pong");

    const auto pong = core.onHubFrame(nlohmann::json{{"type", "pong"}});
    test::check(pong.kind == HubLinkCore::DownKind::kIgnore, "pong is ignored");

    const auto request = core.onHubFrame(nlohmann::json{{"type", "request"},
                                                        {"id", "client1-3"},
                                                        {"method", "orca.ping"}});
    test::check(request.kind == HubLinkCore::DownKind::kForwardRequest && request.id == "client1-3" &&
                    request.method == "orca.ping" && !request.hasParams,
                "request forwards id and method, params omitted stays omitted");

    const auto subscribe = core.onHubFrame(nlohmann::json{{"type", "subscribe"},
                                                          {"id", "s1"},
                                                          {"method", "task.watch"},
                                                          {"params", {{"task", "t9"}}}});
    test::check(subscribe.kind == HubLinkCore::DownKind::kForwardSubscribe &&
                    subscribe.hasParams && subscribe.params.value("task", "") == "t9",
                "subscribe forwards id, method and params");

    const auto unsubscribe = core.onHubFrame(nlohmann::json{{"type", "unsubscribe"}, {"id", "s1"}});
    test::check(unsubscribe.kind == HubLinkCore::DownKind::kForwardUnsubscribe &&
                unsubscribe.id == "s1", "unsubscribe forwards the id");

    // --- timings ------------------------------------------------------------
    test::check(HubLinkCore::backoffMs(0, 0.0) == 400 &&
                    HubLinkCore::backoffMs(0, 1.0) == 600,
                "backoff starts at 500ms +/-20%");
    test::check(HubLinkCore::backoffMs(10, 0.0) >= 24'000 &&
                    HubLinkCore::backoffMs(10, 0.0) <= 30'000 &&
                    HubLinkCore::backoffMs(99, 0.5) == 30'000,
                "backoff caps at 30s");
    test::check(HubLinkCore::backoffMs(3, 0.0) == HubLinkCore::backoffMs(3, 0.0),
                "backoff is deterministic for a fixed jitter");
    test::check(!HubLinkCore::isSilent(100'000, 100'000 - 25'000) &&
                    HubLinkCore::isSilent(100'000, 100'000 - 75'000),
                "silence boundary sits at 75s");

    return test::finish("hub_link_core_test");
}
