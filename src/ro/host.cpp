#include "host.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "address.h"
#include "log.h"
#include "packets.h"
#include "protocol.h"
#include "transport.h"
#include "wire.h"

namespace ro {

namespace {

struct SessionState {
    std::shared_ptr<Connection> connection;
    FrameParser parser;
    std::mutex mutex;
};

}  // namespace

struct Host::Impl {
    Host *owner = nullptr;
    std::shared_ptr<ServerTransport> server;
    std::shared_ptr<ClientTransport> registryClient;
    std::string registryUrl;

    mutable std::mutex mutex;
    std::map<std::string, SourceBase *> sources;
    std::map<std::string, std::vector<std::weak_ptr<Connection>>> subscribers;
    std::map<Connection *, std::shared_ptr<SessionState>> sessions;

    Address address;
    std::string url;
    std::string lastError;
    bool listening = false;
    bool callbacksBound = false;

    void bindCallbacks();
    void handleRecv(std::shared_ptr<Connection> &connection, std::string_view data);
    void handleDisconnect(Connection *connection);
    void handlePacket(SessionState &state, PacketType type, std::string_view body);
    void sendInit(Connection *connection, const std::string &name);
    void sendInitToSubscribers(const std::string &name);
    void sendToSubscribers(const std::string &name, PacketType type, const std::string &body);
    void subscribe(const std::string &name, const std::shared_ptr<Connection> &connection);
    void unsubscribe(Connection *connection);
    std::vector<std::shared_ptr<Connection>> subscribersOf(const std::string &name);
    SourceBase *lookup(const std::string &name);
    void announce(const std::string &name, const std::string &typeName, bool add);
    void announceAll();
    void stopServer();
};

void Host::Impl::bindCallbacks()
{
    if (callbacksBound)
        return;
    callbacksBound = true;

    server->onAccepted([this](std::shared_ptr<Connection> &connection) {
        std::lock_guard<std::mutex> lock(mutex);
        auto &state = sessions[connection.get()];
        if (!state)
            state = std::make_shared<SessionState>();
        state->connection = connection;
        RO_DEBUG("host: client connected from %s", connection->remote().c_str());
    });

    server->onDisconnected([this](Connection *connection) { handleDisconnect(connection); });

    server->onData([this](std::shared_ptr<Connection> &connection, std::string_view data) {
        handleRecv(connection, data);
    });
}

void Host::Impl::handleRecv(std::shared_ptr<Connection> &connection, std::string_view data)
{
    std::shared_ptr<SessionState> state;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = sessions.find(connection.get());
        if (it == sessions.end()) {
            state = std::make_shared<SessionState>();
            state->connection = connection;
            sessions[connection.get()] = state;
        } else {
            state = it->second;
        }
    }

    std::lock_guard<std::mutex> sessionLock(state->mutex);
    state->parser.append(data);
    if (state->parser.failed()) {
        RO_ERROR("host: protocol error, closing connection");
        state->connection->close();
        return;
    }
    PacketType type = PacketType::Invalid;
    std::string_view body;
    while (state->parser.next(type, body))
        handlePacket(*state, type, body);
}

void Host::Impl::handleDisconnect(Connection *connection)
{
    RO_DEBUG("host: client disconnected");
    std::lock_guard<std::mutex> lock(mutex);
    sessions.erase(connection);
    for (auto it = subscribers.begin(); it != subscribers.end();) {
        auto &list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [connection](const std::weak_ptr<Connection> &weak) {
                                      auto strong = weak.lock();
                                      return !strong || strong.get() == connection;
                                  }),
                   list.end());
        if (list.empty())
            it = subscribers.erase(it);
        else
            ++it;
    }
}

SourceBase *Host::Impl::lookup(const std::string &name)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = sources.find(name);
    return it == sources.end() ? nullptr : it->second;
}

void Host::Impl::subscribe(const std::string &name,
                           const std::shared_ptr<Connection> &connection)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto &list = subscribers[name];
    for (const auto &weak : list) {
        auto strong = weak.lock();
        if (strong && strong.get() == connection.get())
            return;
    }
    list.push_back(connection);
}

void Host::Impl::unsubscribe(Connection *connection)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = subscribers.begin(); it != subscribers.end();) {
        auto &list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [connection](const std::weak_ptr<Connection> &weak) {
                                      auto strong = weak.lock();
                                      return !strong || strong.get() == connection;
                                  }),
                   list.end());
        if (list.empty())
            it = subscribers.erase(it);
        else
            ++it;
    }
}

std::vector<std::shared_ptr<Connection>> Host::Impl::subscribersOf(const std::string &name)
{
    std::vector<std::shared_ptr<Connection>> alive;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = subscribers.find(name);
    if (it == subscribers.end())
        return alive;
    for (const auto &weak : it->second) {
        if (auto strong = weak.lock())
            alive.push_back(std::move(strong));
    }
    return alive;
}

void Host::Impl::sendInit(Connection *connection, const std::string &name)
{
    SourceBase *source = lookup(name);
    if (!source || !connection)
        return;

    const auto &names = source->propertyNames();
    const Value::List values = source->propertyValues();
    Value::Map properties;
    for (size_t i = 0; i < names.size() && i < values.size(); ++i)
        properties.emplace_back(names[i], values[i]);
    if (names.size() != values.size())
        RO_ERROR("host: source '%s' returns %zu values for %zu properties", name.c_str(),
                 values.size(), names.size());

    connection->send(makeFrame(PacketType::Init,
                               packets::init(name, source->typeName(), source->signature(),
                                             properties)));
}

void Host::Impl::sendInitToSubscribers(const std::string &name)
{
    for (const auto &connection : subscribersOf(name))
        sendInit(connection.get(), name);
}

void Host::Impl::sendToSubscribers(const std::string &name, PacketType type,
                                   const std::string &body)
{
    const auto connections = subscribersOf(name);
    if (connections.empty())
        return;
    const std::string frame = makeFrame(type, body);
    for (const auto &connection : connections)
        connection->send(frame);
}

void Host::Impl::handlePacket(SessionState &state, PacketType type, std::string_view body)
{
    Reader reader(body);
    switch (type) {
    case PacketType::Handshake: {
        const uint16_t version = reader.u16();
        if (!reader.ok() || version != kProtocolVersion) {
            RO_ERROR("host: protocol version mismatch (client %u, server %u)", version,
                     kProtocolVersion);
            state.connection->send(makeFrame(
                PacketType::Error,
                packets::error(ErrorCode::ProtocolMismatch, "protocol version mismatch", "")));
            state.connection->close();
        }
        break;
    }
    case PacketType::Acquire: {
        const std::string name(reader.bytes());
        if (!reader.ok() || name.empty())
            return;
        subscribe(name, state.connection);
        if (lookup(name)) {
            sendInit(state.connection.get(), name);
        } else {
            RO_DEBUG("host: acquire of unknown object '%s'", name.c_str());
            state.connection->send(makeFrame(
                PacketType::Error,
                packets::error(ErrorCode::SourceNotRegistered, "source not registered", name)));
        }
        break;
    }
    case PacketType::RemoveObject: {
        const std::string name(reader.bytes());
        if (reader.ok())
            unsubscribe(state.connection.get());
        break;
    }
    case PacketType::Invoke: {
        const std::string name(reader.bytes());
        const auto method = static_cast<uint16_t>(reader.varint());
        const uint64_t count = reader.varint();
        Value::List args;
        for (uint64_t i = 0; i < count && reader.ok(); ++i) {
            Value arg;
            if (!decodeValue(reader, arg))
                break;
            args.push_back(std::move(arg));
        }
        const int64_t serial = reader.svarint();
        if (!reader.ok()) {
            RO_ERROR("host: malformed Invoke packet");
            return;
        }

        SourceBase *source = lookup(name);
        if (!source) {
            state.connection->send(makeFrame(
                PacketType::Error,
                packets::error(ErrorCode::SourceNotRegistered, "source not registered", name)));
            return;
        }
        const Value result = source->invokeMethod(method, args);
        state.connection->send(
            makeFrame(PacketType::InvokeReply, packets::invokeReply(serial, result)));
        break;
    }
    case PacketType::Ping: {
        const uint64_t token = reader.varint();
        state.connection->send(makeFrame(PacketType::Pong, packets::pong(token)));
        break;
    }
    case PacketType::RegistryAnnounce: {
        const std::string name(reader.bytes());
        const std::string typeName(reader.bytes());
        const std::string hostUrl(reader.bytes());
        if (reader.ok())
            owner->onRegistryAnnounce(name, typeName, hostUrl);
        break;
    }
    case PacketType::RegistryRemove: {
        const std::string name(reader.bytes());
        if (reader.ok())
            owner->onRegistryRemove(name);
        break;
    }
    case PacketType::Error:
    case PacketType::Init:
    case PacketType::InvokeReply:
    case PacketType::Signal:
    case PacketType::PropertyChange:
    case PacketType::Pong:
        break;
    case PacketType::Invalid:
        RO_WARN("host: ignoring invalid packet");
        break;
    }
}

void Host::Impl::announce(const std::string &name, const std::string &typeName, bool add)
{
    if (!registryClient || registryUrl.empty())
        return;
    const std::string body = add ? packets::registryAnnounce(name, typeName, owner->advertisedUrl())
                                 : packets::registryRemove(name);
    registryClient->send(
        makeFrame(add ? PacketType::RegistryAnnounce : PacketType::RegistryRemove, body));
}

void Host::Impl::announceAll()
{
    if (registryUrl.empty())
        return;
    std::vector<std::pair<std::string, std::string>> entries;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto &entry : sources)
            entries.emplace_back(entry.first, entry.second->typeName());
    }
    for (const auto &entry : entries)
        announce(entry.first, entry.second, true);
}

void Host::Impl::stopServer()
{
    if (listening) {
        server->stop();
        listening = false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    sessions.clear();
    subscribers.clear();
}

Host::Host() : impl_(std::make_unique<Impl>())
{
    impl_->owner = this;
}

Host::~Host()
{
    if (impl_->registryClient)
        impl_->registryClient->stop();
    impl_->stopServer();
}

bool Host::listen(const std::string &address)
{
    Address parsed;
    std::string error;
    if (!parseAddress(address, parsed, error)) {
        impl_->lastError = error;
        RO_ERROR("host: %s", error.c_str());
        return false;
    }
    if (impl_->listening) {
        impl_->lastError = "a server was already created for this host";
        RO_ERROR("host: %s", impl_->lastError.c_str());
        return false;
    }

    impl_->server = makeServer(parsed.url);
    impl_->bindCallbacks();
    const std::string bindHost = parsed.host.empty() ? "0.0.0.0" : parsed.host;
    if (!impl_->server->listen(bindHost, parsed.port)) {
        impl_->lastError = "failed to listen on " + parsed.url;
        RO_ERROR("host: %s", impl_->lastError.c_str());
        return false;
    }

    impl_->address = parsed;
    impl_->url = parsed.url;
    impl_->listening = true;
    RO_DEBUG("host: listening on %s", impl_->url.c_str());
    onListened(impl_->url);
    impl_->announceAll();
    return true;
}

void Host::stop()
{
    impl_->stopServer();
}

bool Host::isListening() const
{
    return impl_->listening;
}

std::string Host::url() const
{
    return impl_->url;
}

const std::string &Host::lastError() const
{
    return impl_->lastError;
}

bool Host::enableRemoting(SourceBase *source, std::string name)
{
    if (!source)
        return false;
    if (name.empty())
        name = source->typeName();
    if (name.empty()) {
        impl_->lastError = "missing object name";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto it = impl_->sources.find(name);
        if (it != impl_->sources.end()) {
            if (it->second == source)
                return true;
            impl_->lastError = "object name '" + name + "' is already registered";
            RO_ERROR("host: %s", impl_->lastError.c_str());
            return false;
        }
        impl_->sources[name] = source;
    }

    source->host_ = this;
    source->name_ = name;

    impl_->sendInitToSubscribers(name);
    onSourceRegistered(name, source->typeName());
    impl_->announce(name, source->typeName(), true);
    return true;
}

bool Host::disableRemoting(SourceBase *source)
{
    std::string name;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        for (auto it = impl_->sources.begin(); it != impl_->sources.end(); ++it) {
            if (it->second == source) {
                name = it->first;
                impl_->sources.erase(it);
                break;
            }
        }
    }
    if (name.empty())
        return false;

    source->host_ = nullptr;
    source->name_.clear();
    impl_->sendToSubscribers(name, PacketType::RemoveObject, packets::removeObject(name));
    onSourceUnregistered(name);
    impl_->announce(name, {}, false);
    return true;
}

bool Host::setRegistryUrl(const std::string &address)
{
    Address parsed;
    std::string error;
    if (!parseAddress(address, parsed, error)) {
        impl_->lastError = error;
        RO_ERROR("host: %s", error.c_str());
        return false;
    }

    impl_->registryUrl = parsed.url;
    auto client = makeClient(parsed.url);
    client->onConnected([this]() {
        RO_DEBUG("host: connected to registry %s", impl_->registryUrl.c_str());
        impl_->registryClient->send(
            makeFrame(PacketType::Handshake, packets::handshake(kProtocolVersion)));
        impl_->announceAll();
    });
    client->setAutoReconnect(true, 1);

    const std::string host = parsed.host.empty() ? "127.0.0.1" : parsed.host;
    if (!client->start(host, parsed.port)) {
        impl_->lastError = "failed to connect to registry " + parsed.url;
        RO_ERROR("host: %s", impl_->lastError.c_str());
        return false;
    }
    impl_->registryClient = std::move(client);
    return true;
}

void Host::broadcastSignal(const std::string &object, uint16_t index, const Value::List &args)
{
    impl_->sendToSubscribers(object, PacketType::Signal, packets::signal(object, index, args));
}

void Host::broadcastProperty(const std::string &object, uint16_t index, const Value &value)
{
    impl_->sendToSubscribers(object, PacketType::PropertyChange,
                             packets::propertyChange(object, index, value));
}

void Host::onSourceRegistered(const std::string &, const std::string &)
{
}

void Host::onSourceUnregistered(const std::string &)
{
}

void Host::onRegistryAnnounce(const std::string &, const std::string &, const std::string &)
{
}

void Host::onRegistryRemove(const std::string &)
{
}

void Host::onListened(const std::string &)
{
}

std::string Host::advertisedUrl() const
{
    return impl_->url;
}

}  // namespace ro
