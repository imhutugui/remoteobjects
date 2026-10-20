// remoteobject - local signals that can also travel over the wire.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>

#include "value.h"

namespace ro {

// Implemented by Source: turns a local emission into packets for replicas.
class SignalTransport {
public:
    virtual ~SignalTransport() = default;
    virtual void emitRemoteSignal(uint16_t index, Value::List args) = 0;
    virtual void pushRemoteProperty(uint16_t index, const Value &value) = 0;
};

// A signal with local subscribers. When attached to a transport, emitting also
// pushes to every connected replica.
template <class... Args>
class Signal {
public:
    using Handler = std::function<void(Args...)>;

    Signal() = default;
    Signal(const Signal &) = delete;
    Signal &operator=(const Signal &) = delete;

    void attachSignal(SignalTransport *transport, uint16_t index)
    {
        transport_ = transport;
        index_ = index;
        property_ = false;
    }

    void attachProperty(SignalTransport *transport, uint16_t index)
    {
        transport_ = transport;
        index_ = index;
        property_ = true;
    }

    uint64_t connect(Handler handler)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint64_t id = nextId_++;
        handlers_.emplace(id, std::move(handler));
        return id;
    }

    bool disconnect(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return handlers_.erase(id) > 0;
    }

    bool hasConnections() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return !handlers_.empty();
    }

    void emit(Args... args)
    {
        std::map<uint64_t, Handler> local;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            local = handlers_;
        }
        for (auto &entry : local)
            entry.second(args...);

        if (!transport_)
            return;
        Value::List values{toValue(args)...};
        if (property_)
            transport_->pushRemoteProperty(index_, values.empty() ? Value() : values.front());
        else
            transport_->emitRemoteSignal(index_, std::move(values));
    }

private:
    SignalTransport *transport_ = nullptr;
    uint16_t index_ = 0;
    bool property_ = false;
    mutable std::mutex mutex_;
    std::map<uint64_t, Handler> handlers_;
    uint64_t nextId_ = 1;
};

}  // namespace ro
