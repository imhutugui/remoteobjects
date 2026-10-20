// remoteobject - address parsing ("tcp://host:port", "ws://host:port").
#pragma once

#include <cstdint>
#include <string>

namespace ro {

enum class Transport {
    Tcp,
    Websocket,
};

struct Address {
    Transport transport = Transport::Tcp;
    std::string host;  // empty means "any interface" when listening
    uint16_t port = 0;
    std::string url;  // normalized "tcp://host:port" / "ws://host:port"
};

// Accepts "tcp://host:port", "ws://host:port", "host:port" and "tcp://:port".
// A bare "host:port" is treated as TCP.
bool parseAddress(const std::string &input, Address &out, std::string &error);

}  // namespace ro
