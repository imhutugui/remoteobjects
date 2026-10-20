// remoteobject - packet payload builders.
#pragma once

#include <cstdint>
#include <string>

#include "value.h"

namespace ro {

// Mirrors QRemoteObjectNode::ErrorCode.
enum class ErrorCode : uint16_t {
    NoError = 0,
    RegistryNotAcquired,
    RegistryAlreadyHosted,
    NodeIsNoServer,
    ServerAlreadyCreated,
    UnintendedRegistryHosting,
    OperationNotValidOnClientNode,
    SourceNotRegistered,
    MissingObjectName,
    HostUrlInvalid,
    ProtocolMismatch,
    ListenFailed,
    SignatureMismatch,
};

namespace packets {

std::string handshake(uint16_t version);
std::string acquire(const std::string &name);
std::string init(const std::string &name, const std::string &typeName, const std::string &signature,
                 const Value::Map &properties);
std::string removeObject(const std::string &name);
std::string invoke(const std::string &name, uint16_t method, const Value::List &args,
                   int64_t serialId);
std::string invokeReply(int64_t serialId, const Value &value);
std::string signal(const std::string &name, uint16_t index, const Value::List &args);
std::string propertyChange(const std::string &name, uint16_t index, const Value &value);
std::string ping(uint64_t token);
std::string pong(uint64_t token);
std::string error(ErrorCode code, const std::string &message,
                  const std::string &objectName);

// Registry control packets.
std::string registryAnnounce(const std::string &name, const std::string &typeName,
                             const std::string &hostUrl);
std::string registryRemove(const std::string &name);

}  // namespace packets

const char *errorCodeName(ErrorCode code);

}  // namespace ro
