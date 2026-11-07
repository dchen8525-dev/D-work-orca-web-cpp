#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace orca::protocol {

// Verbatim wire strings from
// D:/work/orca-web/src/gateway/orca/client-capabilities.ts
// (ORCA_WEB_CLIENT_CAPABILITIES). Orca refuses agentSession.* to a paired
// client that never advertised structured-session support.
//
// Replay this on EVERY Orca socket immediately after the E2EE handshake,
// not once per process -- see OrcaConnection.connect() in rpc-client.ts.
//
// The TypeScript array has 17 entries. An earlier research note said 18;
// the source file is authoritative. tests/vectors/p1-vectors.json carries an
// independent SHA-256 digest over the ordered JSON array so a transcription
// error here fails the build's protocol test instead of failing at runtime
// against a real Orca instance.
inline constexpr std::size_t kClientCapabilityCount = 17;

inline constexpr std::array<std::string_view, kClientCapabilityCount> kClientCapabilities = {
    "agent-session.structured.v1",
    "agent-session.structured.claude.v1",
    "agent-session.structured.registered-agents.v1",
    "agent-session.structured.client-launch-mode.v1",
    "agent-session.options.optional-model.v1",
    "agent-session.pending-send-result.v1",
    "agent-session.turn-item.v1",
    "agent-session.current-requests.v1",
    "agent-session.commands-on-request.v1",
    "agent-session.coherent-model-catalog.v1",
    "agent-session.session-boundary.v1",
    "agent-session.background-task-stop.v1",
    "agent-session.background-task-row-stop.v1",
    "agent-session.background-task-child-views.v1",
    "agentSession.create.message.v1",
    "session-tabs.close-intent.v1",
    "session-tabs.authoritative-inventory.v1",
};

inline constexpr char kClientCapabilitiesMethod[] = "runtime.clientCapabilities.update";

// {"clientCapabilities":[...]} preserving the original array order.
nlohmann::json makeClientCapabilitiesParams();

// Full Orca JSON-RPC request: {id, deviceToken, method, params}.
// sendRpc() in rpc-client.ts uses exactly this shape. P2 will generalise it
// into a shared request builder; keeping the mandatory post-handshake replay
// self-contained here makes it hard to forget.
nlohmann::json makeClientCapabilitiesRequest(const std::string& id,
                                             const std::string& deviceToken);

}  // namespace orca::protocol
