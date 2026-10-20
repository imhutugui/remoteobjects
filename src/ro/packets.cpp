#include "packets.h"

#include "protocol.h"
#include "wire.h"

namespace ro {

namespace packets {

std::string handshake(uint16_t version)
{
    Writer w;
    w.u16(version);
    return w.take();
}

std::string acquire(const std::string &name)
{
    Writer w;
    w.bytes(name);
    return w.take();
}

std::string init(const std::string &name, const std::string &typeName, const std::string &signature,
                 const Value::Map &properties)
{
    Writer w;
    w.bytes(name);
    w.bytes(typeName);
    w.bytes(signature);
    w.varint(properties.size());
    for (const auto &entry : properties) {
        w.bytes(entry.first.str());
        encodeValue(w, entry.second);
    }
    return w.take();
}

std::string removeObject(const std::string &name)
{
    Writer w;
    w.bytes(name);
    return w.take();
}

std::string invoke(const std::string &name, uint16_t method, const Value::List &args,
                   int64_t serialId)
{
    Writer w;
    w.bytes(name);
    w.varint(method);
    w.varint(args.size());
    for (const auto &arg : args)
        encodeValue(w, arg);
    w.svarint(serialId);
    return w.take();
}

std::string invokeReply(int64_t serialId, const Value &value)
{
    Writer w;
    w.svarint(serialId);
    encodeValue(w, value);
    return w.take();
}

std::string signal(const std::string &name, uint16_t index, const Value::List &args)
{
    Writer w;
    w.bytes(name);
    w.varint(index);
    w.varint(args.size());
    for (const auto &arg : args)
        encodeValue(w, arg);
    return w.take();
}

std::string propertyChange(const std::string &name, uint16_t index, const Value &value)
{
    Writer w;
    w.bytes(name);
    w.varint(index);
    encodeValue(w, value);
    return w.take();
}

std::string ping(uint64_t token)
{
    Writer w;
    w.varint(token);
    return w.take();
}

std::string pong(uint64_t token)
{
    return ping(token);
}

std::string error(ErrorCode code, const std::string &message, const std::string &objectName)
{
    Writer w;
    w.varint(static_cast<uint64_t>(code));
    w.bytes(message);
    w.bytes(objectName);
    return w.take();
}

std::string registryAnnounce(const std::string &name, const std::string &typeName,
                             const std::string &hostUrl)
{
    Writer w;
    w.bytes(name);
    w.bytes(typeName);
    w.bytes(hostUrl);
    return w.take();
}

std::string registryRemove(const std::string &name)
{
    Writer w;
    w.bytes(name);
    return w.take();
}

}  // namespace packets

const char *errorCodeName(ErrorCode code)
{
    switch (code) {
    case ErrorCode::NoError: return "NoError";
    case ErrorCode::RegistryNotAcquired: return "RegistryNotAcquired";
    case ErrorCode::RegistryAlreadyHosted: return "RegistryAlreadyHosted";
    case ErrorCode::NodeIsNoServer: return "NodeIsNoServer";
    case ErrorCode::ServerAlreadyCreated: return "ServerAlreadyCreated";
    case ErrorCode::UnintendedRegistryHosting: return "UnintendedRegistryHosting";
    case ErrorCode::OperationNotValidOnClientNode: return "OperationNotValidOnClientNode";
    case ErrorCode::SourceNotRegistered: return "SourceNotRegistered";
    case ErrorCode::MissingObjectName: return "MissingObjectName";
    case ErrorCode::HostUrlInvalid: return "HostUrlInvalid";
    case ErrorCode::ProtocolMismatch: return "ProtocolMismatch";
    case ErrorCode::ListenFailed: return "ListenFailed";
    case ErrorCode::SignatureMismatch: return "SignatureMismatch";
    }
    return "Unknown";
}

}  // namespace ro
