// remoteobject - transport abstraction over TCP and WebSocket.
//
// All transports carry raw byte frames (protocol.h); on the wire the frames
// look identical, only the carrier differs:
//   tcp://host:port  - plain TCP, frames reassembled by FrameParser
//   ws://host:port   - WebSocket, frames delivered as binary frames
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ro {

// One byte-oriented connection (a socket or a WebSocket).
class Connection {
public:
    virtual ~Connection() = default;
    // Asynchronous send; data may be sent fragmented but ordering is preserved.
    virtual void send(std::string frame) = 0;
    virtual void close() = 0;
    virtual std::string remote() const = 0;
};

// Outbound side: node to host, and host to registry.
class ClientTransport {
public:
    using ConnectedFn = std::function<void()>;
    using DisconnectedFn = std::function<void()>;
    using DataFn = std::function<void(std::string_view)>;

    virtual ~ClientTransport() = default;

    // Starts a non-blocking connection; the connect/disconnect/data callbacks
    // fire on the IO thread.
    virtual bool start(std::string host, uint16_t port) = 0;
    virtual void stop() = 0;
    virtual void send(std::string frame) = 0;
    virtual void onConnected(ConnectedFn) = 0;
    virtual void onDisconnected(DisconnectedFn) = 0;
    virtual void onData(DataFn) = 0;
    virtual void setAutoReconnect(bool enable, uint32_t delaySeconds) = 0;
};

// Inbound side: host listening for nodes.
class ServerTransport {
public:
    using AcceptedFn = std::function<void(std::shared_ptr<Connection> &)>;
    using DisconnectedFn = std::function<void(Connection *)>;
    using DataFn = std::function<void(std::shared_ptr<Connection> &, std::string_view)>;

    virtual ~ServerTransport() = default;

    virtual bool listen(std::string host, uint16_t port) = 0;
    virtual void stop() = 0;
    virtual void onAccepted(AcceptedFn) = 0;
    virtual void onDisconnected(DisconnectedFn) = 0;
    virtual void onData(DataFn) = 0;
};

// Factories: pick the carrier from the URL scheme ("tcp://", "ws://").
std::shared_ptr<ClientTransport> makeClient(const std::string &url);
std::shared_ptr<ServerTransport> makeServer(const std::string &url);

}  // namespace ro
