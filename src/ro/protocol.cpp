#include "protocol.h"

#include "wire.h"

namespace ro {

std::string makeFrame(PacketType type, std::string_view body)
{
    Writer w;
    w.u32(static_cast<uint32_t>(body.size()));
    w.u16(static_cast<uint16_t>(type));
    w.raw(body);
    return w.take();
}

void FrameParser::append(std::string_view data)
{
    if (failed_)
        return;
    // Compact consumed bytes before growing the buffer.
    if (off_ > 0 && off_ == buf_.size()) {
        buf_.clear();
        off_ = 0;
    } else if (off_ > 4096) {
        buf_.erase(0, off_);
        off_ = 0;
    }
    buf_.append(data);
}

bool FrameParser::next(PacketType &type, std::string_view &body)
{
    if (failed_)
        return false;
    const size_t available = buf_.size() - off_;
    if (available < 6)
        return false;

    Reader header(std::string_view(buf_).substr(off_, 6));
    const uint32_t len = header.u32();
    const auto rawType = header.u16();
    if (len > kMaxFrameSize) {
        failed_ = true;
        return false;
    }
    if (available < 6 + static_cast<size_t>(len))
        return false;

    type = static_cast<PacketType>(rawType);
    body = std::string_view(buf_).substr(off_ + 6, len);
    off_ += 6 + len;
    return true;
}

const char *packetTypeName(PacketType type)
{
    switch (type) {
    case PacketType::Handshake: return "Handshake";
    case PacketType::Acquire: return "Acquire";
    case PacketType::Init: return "Init";
    case PacketType::RemoveObject: return "RemoveObject";
    case PacketType::Invoke: return "Invoke";
    case PacketType::InvokeReply: return "InvokeReply";
    case PacketType::Signal: return "Signal";
    case PacketType::PropertyChange: return "PropertyChange";
    case PacketType::Ping: return "Ping";
    case PacketType::Pong: return "Pong";
    case PacketType::Error: return "Error";
    case PacketType::RegistryAnnounce: return "RegistryAnnounce";
    case PacketType::RegistryRemove: return "RegistryRemove";
    case PacketType::Invalid: return "Invalid";
    }
    return "Unknown";
}

}  // namespace ro
