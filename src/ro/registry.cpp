#include "registry.h"

#include "log.h"

namespace ro {

RegistrySource::RegistrySource() : SourceBase(registry::kTypeName, registry::kSignature)
{
    setPropertyNames({"sources", "types"});
    sourcesChanged.attachProperty(this, kRegistrySources);
    typesChanged.attachProperty(this, kRegistryTypes);
}

void RegistrySource::setEntries(registry::EntryMap sources, registry::EntryMap types)
{
    registry::EntryMap sourcesCopy;
    registry::EntryMap typesCopy;
    bool sourcesDiffer = false;
    bool typesDiffer = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sourcesDiffer = sources_ != sources;
        typesDiffer = types_ != types;
        sources_ = std::move(sources);
        types_ = std::move(types);
        sourcesCopy = sources_;
        typesCopy = types_;
    }
    if (sourcesDiffer)
        sourcesChanged.emit(sourcesCopy);
    if (typesDiffer)
        typesChanged.emit(typesCopy);
}

Value::List RegistrySource::propertyValues() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return {toValue(sources_), toValue(types_)};
}

Value RegistrySource::invokeMethod(uint16_t, const Value::List &)
{
    return Value();
}

RegistryReplica::RegistryReplica(Node *node, std::string name)
    : Replica(node, std::move(name), registry::kTypeName, registry::kSignature)
{
    registerProperty("sources", [this](const Value &value) {
        auto entries = fromValue<registry::EntryMap>(value);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sources_ = std::move(entries);
        }
        sourcesChanged.emit(sources());
    });
    registerProperty("types", [this](const Value &value) {
        auto entries = fromValue<registry::EntryMap>(value);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            types_ = std::move(entries);
        }
        typesChanged.emit(types());
    });}

registry::EntryMap RegistryReplica::sources() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sources_;
}

registry::EntryMap RegistryReplica::types() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return types_;
}

RegistryHost::RegistryHost()
{
    registrySource_ = new RegistrySource();
    Host::enableRemoting(registrySource_, registry::kObjectName);
}

RegistryHost::~RegistryHost()
{
    Host::disableRemoting(registrySource_);
    delete registrySource_;
}

void RegistryHost::onSourceRegistered(const std::string &name, const std::string &typeName)
{
    if (name == registry::kObjectName)
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_[name] = Entry{typeName, advertisedUrl()};
    }
    publish();
}

void RegistryHost::onSourceUnregistered(const std::string &name)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.erase(name);
    }
    publish();
}

void RegistryHost::onRegistryAnnounce(const std::string &name, const std::string &typeName,
                                      const std::string &hostUrl)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_[name] = Entry{typeName, hostUrl};
    }
    RO_DEBUG("registry: '%s' (%s) at %s", name.c_str(), typeName.c_str(), hostUrl.c_str());
    publish();
}

void RegistryHost::onRegistryRemove(const std::string &name)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.erase(name);
    }
    publish();
}

void RegistryHost::onListened(const std::string &url)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &entry : entries_) {
            if (entry.second.hostUrl.empty())
                entry.second.hostUrl = url;
        }
    }
    publish();
}

void RegistryHost::publish()
{
    registry::EntryMap sources;
    registry::EntryMap types;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &entry : entries_) {
            sources[entry.first] = entry.second.hostUrl;
            types[entry.first] = entry.second.typeName;
        }
    }
    registrySource_->setEntries(std::move(sources), std::move(types));
}

}  // namespace ro
