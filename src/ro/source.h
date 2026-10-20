// remoteobject - source side of a remote object (what a QtRO "Source" is).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "signal.h"
#include "value.h"

namespace ro {

class Host;

// Base class for generated `*SimpleSource` types. It owns nothing but the
// remote side bookkeeping: the generated subclass holds the actual state.
class SourceBase : public SignalTransport {
public:
    SourceBase(std::string typeName, std::string signature);
    ~SourceBase() override = default;

    const std::string &typeName() const { return typeName_; }
    const std::string &signature() const { return signature_; }
    const std::string &name() const { return name_; }
    const std::vector<std::string> &propertyNames() const { return propertyNames_; }

    // Snapshot of all properties, used for the Init packet.
    virtual Value::List propertyValues() const = 0;
    // Dispatches a remote invocation; returns the reply value.
    virtual Value invokeMethod(uint16_t index, const Value::List &args) = 0;

    void emitRemoteSignal(uint16_t index, Value::List args) override;
    void pushRemoteProperty(uint16_t index, const Value &value) override;

protected:
    void setPropertyNames(std::vector<std::string> names) { propertyNames_ = std::move(names); }

private:
    friend class Host;

    Host *host_ = nullptr;
    std::string typeName_;
    std::string signature_;
    std::string name_;
    std::vector<std::string> propertyNames_;
};

}  // namespace ro
