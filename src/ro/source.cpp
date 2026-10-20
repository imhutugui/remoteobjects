#include "source.h"

#include "host.h"

namespace ro {

SourceBase::SourceBase(std::string typeName, std::string signature)
    : typeName_(std::move(typeName)), signature_(std::move(signature))
{
}

void SourceBase::emitRemoteSignal(uint16_t index, Value::List args)
{
    if (host_ && !name_.empty())
        host_->broadcastSignal(name_, index, args);
}

void SourceBase::pushRemoteProperty(uint16_t index, const Value &value)
{
    if (host_ && !name_.empty())
        host_->broadcastProperty(name_, index, value);
}

}  // namespace ro
