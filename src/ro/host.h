// remoteobject - host side: listens for nodes and publishes sources.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "source.h"

namespace ro {

class Host {
public:
    Host();
    virtual ~Host();

    Host(const Host &) = delete;
    Host &operator=(const Host &) = delete;

    // "tcp://0.0.0.0:4242" or "ws://0.0.0.0:4242". Returns false if the
    // address is invalid or the port cannot be bound.
    bool listen(const std::string &address);
    void stop();
    bool isListening() const;
    std::string url() const;
    const std::string &lastError() const;

    // Publishes a source under `name` (defaults to the source's type name).
    bool enableRemoting(SourceBase *source, std::string name = {});
    bool disableRemoting(SourceBase *source);

    // Announces this host's objects to a registry host.
    bool setRegistryUrl(const std::string &address);

    // Used by sources; safe to call from any thread.
    void broadcastSignal(const std::string &object, uint16_t index, const Value::List &args);
    void broadcastProperty(const std::string &object, uint16_t index, const Value &value);

protected:
    struct Impl;
    Impl *impl() const { return impl_.get(); }

    // RegistryHost hooks.
    virtual void onSourceRegistered(const std::string &name, const std::string &typeName);
    virtual void onSourceUnregistered(const std::string &name);
    virtual void onRegistryAnnounce(const std::string &name, const std::string &typeName,
                                    const std::string &hostUrl);
    virtual void onRegistryRemove(const std::string &name);
    // Called after a successful listen().
    virtual void onListened(const std::string &url);
    // URL advertised to the registry for this host's own objects.
    virtual std::string advertisedUrl() const;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace ro
