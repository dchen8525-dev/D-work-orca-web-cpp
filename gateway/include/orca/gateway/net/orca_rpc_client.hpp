#pragma once

#include <orca/gateway/net/orca_bridge.hpp>

#include <boost/asio/io_context.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace orca::gateway {

// One authenticated Orca runtime socket: E2EE v2 handshake, then sealed
// JSON-RPC (src/gateway/orca/rpc-client.ts). Orca has no HTTP API; every call
// is a frame on this socket.
//
// Threading: all I/O and every callback runs on the io_context passed to
// connect(). The bridge owns that context on its own worker thread.
class OrcaConnection {
public:
    using Ptr = std::shared_ptr<OrcaConnection>;
    using RequestDone = std::function<void(std::optional<nlohmann::json> result,
                                           std::optional<LinkError> error)>;
    // Every response frame for a subscription id, including streamed parts.
    using FrameSink = std::function<void(const nlohmann::json& frame)>;
    // Fired exactly once when an established connection ends; nullopt = clean close.
    using CloseSink = std::function<void(std::optional<LinkError> error)>;
    using ConnectDone = std::function<void(Ptr connection, std::optional<LinkError> error)>;

    struct Options {
        std::string endpoint;        // ws:// or wss://
        std::string deviceToken;
        std::string hostPublicKeyB64;
        std::string transport = "direct";  // "direct" | "relay"
        std::string relayHostId;
        std::uint64_t handshakeTimeoutMs = 15'000;
        std::uint64_t requestTimeoutMs = 30'000;
    };

    // Resolve/connect/handshake and replay the per-socket client capabilities.
    // done() runs exactly once on the io_context thread. onClose is installed
    // before the handshake starts, so a drop right after success is not lost.
    static void connect(boost::asio::io_context& io, Options options, CloseSink onClose,
                        ConnectDone done);

    // Exactly one completion per request. A closed connection fails immediately.
    void request(const std::string& id, std::string method, nlohmann::json params,
                 std::uint64_t timeoutMs, RequestDone done);

    void subscribe(const std::string& id, std::string method, nlohmann::json params,
                   FrameSink sink);
    void unsubscribe(const std::string& id);

    // Graceful close (code 1000); the close sink receives nullopt.
    void close(std::string reason);

    bool open() const;

private:
    struct Impl;
    explicit OrcaConnection(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};

}  // namespace orca::gateway
