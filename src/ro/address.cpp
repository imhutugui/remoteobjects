#include "address.h"

#include <cstdlib>

namespace ro {

bool parseAddress(const std::string &input, Address &out, std::string &error)
{
    std::string rest = input;
    std::string scheme;
    if (rest.rfind("tcp://", 0) == 0) {
        scheme = "tcp";
        rest = rest.substr(6);
        out.transport = Transport::Tcp;
    } else if (rest.rfind("ws://", 0) == 0) {
        scheme = "ws";
        rest = rest.substr(5);
        out.transport = Transport::Websocket;
    } else if (rest.find("://") != std::string::npos) {
        const auto schemeEnd = rest.find("://");
        error = "unsupported URL scheme '" + input.substr(0, schemeEnd + 3)
                + "' (tcp:// and ws:// are supported)";
        return false;
    } else {
        // Bare "host:port" defaults to TCP.
        scheme = "tcp";
        out.transport = Transport::Tcp;
    }

    const size_t colon = rest.rfind(':');
    if (colon == std::string::npos) {
        error = "missing port in address '" + input + "'";
        return false;
    }

    out.host = rest.substr(0, colon);
    const std::string portText = rest.substr(colon + 1);
    if (portText.empty()) {
        error = "missing port in address '" + input + "'";
        return false;
    }
    char *end = nullptr;
    const long port = std::strtol(portText.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || port <= 0 || port > 65535) {
        error = "invalid port in address '" + input + "'";
        return false;
    }
    out.port = static_cast<uint16_t>(port);
    out.url = scheme + "://" + (out.host.empty() ? std::string("127.0.0.1") : out.host) + ":"
              + std::to_string(out.port);
    return true;
}

}  // namespace ro
