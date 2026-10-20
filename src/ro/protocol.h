// remoteobject - packet framing and protocol constants.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ro {

inline constexpr uint16_t kProtocolVersion = 1;
inline constexpr uint32_t kMaxFrameSize = 64u * 1024u * 1024u;

enum class PacketType : uint16_t {
    Invalid = 0,
    Handshake,       // u32 protocol version
    Acquire,         // str name                      client -> server
    Init,            // str name, str type, str sig, map<string,Value> properties
    RemoveObject,    // str name
    Invoke,          // str name, u16 method, list<Value> args, i64 serial
    InvokeReply,     // i64 serial, Value
    Signal,          // str name, u16 signal, list<Value> args
    PropertyChange,  // str name, u16 property, Value
    Ping,            // u64 token
    Pong,            // u64 token
    Error,           // u16 code, str message
    RegistryAnnounce,  // str name, str typeName, str hostUrl   host -> registry
    RegistryRemove,    // str name                              host -> registry
};

// A frame is [u32 bodyLength][u16 type][body].
std::string makeFrame(PacketType type, std::string_view body);

// Reassembles frames from arbitrarily split or coalesced socket reads.
class FrameParser {
public:
    void append(std::string_view data);
    // Returns false when no complete frame is buffered. `body` is only valid
    // until the next call.
    bool next(PacketType &type, std::string_view &body);
    bool failed() const { return failed_; }
    size_t buffered() const { return buf_.size() - off_; }

private:
    std::string buf_;
    size_t off_ = 0;
    bool failed_ = false;
};

const char *packetTypeName(PacketType type);

}  // namespace ro
