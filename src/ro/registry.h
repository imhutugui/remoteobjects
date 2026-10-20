// remoteobject - built-in registry object (source side and replica side).
#pragma once

#include <map>
#include <mutex>
#include <string>

#include "host.h"
#include "replica.h"
#include "source.h"

namespace ro {

namespace registry {

inline constexpr const char *kObjectName = "rl.registry";
inline constexpr const char *kTypeName = "Registry";
inline constexpr const char *kSignature = "rl.registry/1";

// name -> value, where the value is a host URL for `sources` and a type name
// for `types`.
using EntryMap = std::map<std::string, std::string>;

}  // namespace registry

// Property indexes; also the packet order used by the registry object.
enum RegistryProperty : uint16_t { kRegistrySources = 0, kRegistryTypes = 1 };

class RegistrySource : public SourceBase {
public:
    RegistrySource();

    void setEntries(registry::EntryMap sources, registry::EntryMap types);

    Value::List propertyValues() const override;
    Value invokeMethod(uint16_t index, const Value::List &args) override;

    Signal<registry::EntryMap> sourcesChanged;
    Signal<registry::EntryMap> typesChanged;

private:
    mutable std::mutex mutex_;
    registry::EntryMap sources_;
    registry::EntryMap types_;
};

class RegistryReplica : public Replica {
public:
    explicit RegistryReplica(Node *node, std::string name = registry::kObjectName);

    registry::EntryMap sources() const;
    registry::EntryMap types() const;

    Signal<registry::EntryMap> sourcesChanged;
    Signal<registry::EntryMap> typesChanged;

private:
    mutable std::mutex mutex_;
    registry::EntryMap sources_;
    registry::EntryMap types_;
};

// A host that also serves the built-in registry object, so nodes can discover
// objects by name instead of hard coding a host URL.
class RegistryHost : public Host {
public:
    RegistryHost();
    ~RegistryHost() override;

protected:
    void onSourceRegistered(const std::string &name, const std::string &typeName) override;
    void onSourceUnregistered(const std::string &name) override;
    void onRegistryAnnounce(const std::string &name, const std::string &typeName,
                            const std::string &hostUrl) override;
    void onRegistryRemove(const std::string &name) override;
    void onListened(const std::string &url) override;

private:
    struct Entry {
        std::string typeName;
        std::string hostUrl;
    };

    void publish();

    mutable std::mutex mutex_;
    std::map<std::string, Entry> entries_;
    RegistrySource *registrySource_ = nullptr;
};

}  // namespace ro
