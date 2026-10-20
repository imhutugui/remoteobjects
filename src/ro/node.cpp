#include "node.h"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "address.h"
#include "log.h"
#include "packets.h"
#include "protocol.h"
#include "registry.h"
#include "transport.h"
#include "wire.h"

namespace ro {

namespace {

struct Peer {
    std::shared_ptr<ClientTransport> client;
    std::string url;
    FrameParser parser;
    std::mutex parserMutex;
    std::atomic<bool> connected{false};

    ~Peer()
    {
        if (client)
            client->stop();
    }

    bool isConnected() const { return connected.load(); }
};

}  // namespace

struct Node::Impl {
    Node *owner = nullptr;

    mutable std::mutex mutex;
    std::vector<std::unique_ptr<Replica>> replicas;
    std::map<std::string, std::unique_ptr<Peer>> connections;
    std::map<uint64_t, std::shared_ptr<PendingCall::State>> pending;
    std::map<std::string, std::string> registrySources;
    std::map<std::string, std::string> registryTypes;

    RegistryReplica *registry = nullptr;
    std::string defaultUrl;
    std::string registryUrl;
    std::string lastError;
    std::atomic<uint64_t> nextSerial{1};
    std::atomic<int> connectedCount{0};

    Peer *ensureConnection(const Address &address);
    void sendTo(const std::string &url, PacketType type, const std::string &body);
    void sendOn(Peer *conn, PacketType type, const std::string &body);

    void onConnected(Peer *conn);
    void onDisconnected(Peer *conn);
    void onData(Peer *conn, std::string_view data);
    void handlePacket(Peer *conn, PacketType type, std::string_view body);

    Replica *findReplica(const std::string &name);
    void bindReplica(Replica *replica);
    void onRegistryChanged();
    void finishPending(int64_t serial, const Value &value);
    void stopAll();
};

Peer *Node::Impl::ensureConnection(const Address &address)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = connections.find(address.url);
        if (it != connections.end())
            return it->second.get();
    }

    auto owned = std::make_unique<Peer>();
    owned->url = address.url;
    Peer *conn = owned.get();
    conn->client = makeClient(address.url);

    conn->client->onConnected([this, conn]() {
        conn->connected = true;
        ++connectedCount;
        conn->client->send(makeFrame(PacketType::Handshake, packets::handshake(kProtocolVersion)));
        onConnected(conn);
    });
    conn->client->onDisconnected([this, conn]() {
        conn->connected = false;
        --connectedCount;
        onDisconnected(conn);
    });
    conn->client->onData([this, conn](std::string_view data) { onData(conn, data); });
    conn->client->setAutoReconnect(true, 1);

    {
        std::lock_guard<std::mutex> lock(mutex);
        connections[address.url] = std::move(owned);
    }

    const std::string host = address.host.empty() ? "127.0.0.1" : address.host;
    if (!conn->client->start(host, address.port)) {
        std::lock_guard<std::mutex> lock(mutex);
        lastError = "failed to connect to " + address.url;
        connections.erase(address.url);
        RO_ERROR("node: failed to connect to %s", address.url.c_str());
        return nullptr;
    }
    RO_DEBUG("node: connecting to %s", address.url.c_str());
    return conn;
}

void Node::Impl::sendOn(Peer *conn, PacketType type, const std::string &body)
{
    if (!conn || !conn->isConnected())
        return;
    conn->client->send(makeFrame(type, body));
}

void Node::Impl::sendTo(const std::string &url, PacketType type, const std::string &body)
{
    Peer *conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = connections.find(url);
        if (it != connections.end())
            conn = it->second.get();
    }
    sendOn(conn, type, body);
}

void Node::Impl::onConnected(Peer *conn)
{
    RO_DEBUG("node: connected to %s", conn->url.c_str());
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto &replica : replicas) {
            if (replica->hostUrl() == conn->url && !replica->name().empty())
                names.push_back(replica->name());
        }
    }
    for (const auto &name : names)
        conn->client->send(makeFrame(PacketType::Acquire, packets::acquire(name)));
}

void Node::Impl::onDisconnected(Peer *conn)
{
    RO_DEBUG("node: disconnected from %s", conn->url.c_str());
    std::vector<Replica *> affected;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto &replica : replicas) {
            if (replica->hostUrl() == conn->url)
                affected.push_back(replica.get());
        }
    }
    for (Replica *replica : affected)
        replica->handleSourceRemoved();
}

void Node::Impl::onData(Peer *conn, std::string_view data)
{
    std::lock_guard<std::mutex> lock(conn->parserMutex);
    conn->parser.append(data);
    if (conn->parser.failed()) {
        RO_ERROR("node: protocol error from %s, closing connection", conn->url.c_str());
        conn->client->stop();
        return;
    }
    PacketType type = PacketType::Invalid;
    std::string_view body;
    while (conn->parser.next(type, body))
        handlePacket(conn, type, body);
}

Replica *Node::Impl::findReplica(const std::string &name)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &replica : replicas) {
        if (replica->name() == name)
            return replica.get();
    }
    return nullptr;
}

void Node::Impl::handlePacket(Peer *conn, PacketType type, std::string_view body)
{
    Reader reader(body);
    switch (type) {
    case PacketType::Init: {
        const std::string name(reader.bytes());
        const std::string typeName(reader.bytes());
        const std::string signature(reader.bytes());
        const uint64_t count = reader.varint();
        Value::Map properties;
        for (uint64_t i = 0; i < count && reader.ok(); ++i) {
            const std::string key(reader.bytes());
            Value value;
            if (!decodeValue(reader, value))
                break;
            properties.emplace_back(key, std::move(value));
        }
        if (!reader.ok()) {
            RO_ERROR("node: malformed Init packet");
            return;
        }
        if (Replica *replica = findReplica(name))
            replica->handleInit(typeName, signature, properties);
        else
            RO_WARN("node: Init for unknown replica '%s'", name.c_str());
        break;
    }
    case PacketType::PropertyChange: {
        const std::string name(reader.bytes());
        const auto index = static_cast<uint16_t>(reader.varint());
        Value value;
        if (!decodeValue(reader, value) || !reader.ok())
            return;
        if (Replica *replica = findReplica(name))
            replica->handlePropertyChange(index, value);
        break;
    }
    case PacketType::Signal: {
        const std::string name(reader.bytes());
        const auto index = static_cast<uint16_t>(reader.varint());
        const uint64_t count = reader.varint();
        Value::List args;
        for (uint64_t i = 0; i < count && reader.ok(); ++i) {
            Value arg;
            if (!decodeValue(reader, arg))
                break;
            args.push_back(std::move(arg));
        }
        if (!reader.ok())
            return;
        if (Replica *replica = findReplica(name))
            replica->handleSignal(index, args);
        break;
    }
    case PacketType::InvokeReply: {
        const int64_t serial = reader.svarint();
        Value value;
        if (!decodeValue(reader, value) || !reader.ok())
            return;
        finishPending(serial, value);
        break;
    }
    case PacketType::RemoveObject: {
        const std::string name(reader.bytes());
        if (Replica *replica = findReplica(name))
            replica->handleSourceRemoved();
        break;
    }
    case PacketType::Error: {
        const auto code = static_cast<ErrorCode>(reader.varint());
        const std::string message(reader.bytes());
        const std::string name(reader.bytes());
        RO_WARN("node: error from %s: %s (%s) for '%s'", conn->url.c_str(), errorCodeName(code),
                message.c_str(), name.c_str());
        break;
    }
    case PacketType::Ping: {
        const uint64_t token = reader.varint();
        sendOn(conn, PacketType::Pong, packets::pong(token));
        break;
    }
    case PacketType::Pong:
    case PacketType::Handshake:
    case PacketType::Acquire:
    case PacketType::Invoke:
    case PacketType::RegistryAnnounce:
    case PacketType::RegistryRemove:
        break;
    case PacketType::Invalid:
        RO_WARN("node: ignoring invalid packet from %s", conn->url.c_str());
        break;
    }
}

void Node::Impl::bindReplica(Replica *replica)
{
    const std::string url = replica->hostUrl();
    if (url.empty())
        return;
    Address address;
    std::string error;
    if (!parseAddress(url, address, error)) {
        RO_ERROR("node: replica '%s' has invalid host URL '%s': %s", replica->name().c_str(),
                 url.c_str(), error.c_str());
        return;
    }
    Peer *conn = ensureConnection(address);
    if (conn && conn->isConnected() && !replica->name().empty())
        conn->client->send(makeFrame(PacketType::Acquire, packets::acquire(replica->name())));
}

void Node::Impl::onRegistryChanged()
{
    if (!registry)
        return;
    const auto sources = registry->sources();
    const auto types = registry->types();

    std::vector<std::pair<std::string, std::string>> added;
    std::vector<std::string> removed;
    std::vector<Replica *> toBind;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto &entry : sources) {
            auto known = registrySources.find(entry.first);
            if (known == registrySources.end() || known->second != entry.second) {
                auto type = types.find(entry.first);
                added.emplace_back(entry.first, type == types.end() ? std::string() : type->second);
            }
        }
        for (auto it = registrySources.begin(); it != registrySources.end();) {
            if (sources.find(it->first) == sources.end()) {
                removed.push_back(it->first);
                it = registrySources.erase(it);
            } else {
                ++it;
            }
        }
        registrySources = sources;
        registryTypes = types;

        for (const auto &replica : replicas) {
            if (replica->name().empty())
                continue;
            const std::string current = replica->hostUrl();
            if (!current.empty() && current != registryUrl)
                continue;  // an explicit connection wins
            auto entry = registrySources.find(replica->name());
            if (entry != registrySources.end() && !entry->second.empty()) {
                replica->setHostUrl(entry->second);
                toBind.push_back(replica.get());
            }
        }
    }

    for (const auto &entry : added)
        owner->remoteObjectAdded.emit(entry.first, entry.second);
    for (const auto &name : removed)
        owner->remoteObjectRemoved.emit(name);
    for (Replica *replica : toBind)
        bindReplica(replica);
}

void Node::Impl::finishPending(int64_t serial, const Value &value)
{
    std::shared_ptr<PendingCall::State> state;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = pending.find(static_cast<uint64_t>(serial));
        if (it == pending.end())
            return;
        state = it->second;
        pending.erase(it);
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->value = value;
        state->finished = true;
    }
    state->cv.notify_all();
}

void Node::Impl::stopAll()
{
    std::map<std::string, std::unique_ptr<Peer>> connections;
    {
        std::lock_guard<std::mutex> lock(mutex);
        connections.swap(this->connections);
    }
    for (auto &entry : connections)
        if (entry.second->client)
            entry.second->client->stop();
    connections.clear();
}

Node::Node() : impl_(std::make_unique<Impl>())
{
    impl_->owner = this;
}

Node::~Node()
{
    impl_->stopAll();
    impl_->replicas.clear();
}

bool Node::connectToNode(const std::string &address)
{
    Address parsed;
    std::string error;
    if (!parseAddress(address, parsed, error)) {
        impl_->lastError = error;
        RO_ERROR("node: %s", error.c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->defaultUrl = parsed.url;
    }
    return impl_->ensureConnection(parsed) != nullptr;
}

bool Node::setRegistryUrl(const std::string &address)
{
    Address parsed;
    std::string error;
    if (!parseAddress(address, parsed, error)) {
        impl_->lastError = error;
        RO_ERROR("node: %s", error.c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->registryUrl = parsed.url;
    }
    if (!impl_->ensureConnection(parsed))
        return false;

    auto *registry = new RegistryReplica(this);
    registry->setHostUrl(parsed.url);
    impl_->registry = registry;
    registry->sourcesChanged.connect([this](registry::EntryMap) { impl_->onRegistryChanged(); });
    registry->typesChanged.connect([this](registry::EntryMap) { impl_->onRegistryChanged(); });
    addReplica(std::unique_ptr<Replica>(registry));
    return true;
}

bool Node::waitForConnected(int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (impl_->connectedCount.load() > 0)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return impl_->connectedCount.load() > 0;
}

bool Node::waitForRegistry(int timeoutMs)
{
    if (!impl_->registry)
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (impl_->registry->isReplicaValid())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return impl_->registry->isReplicaValid();
}

std::string Node::registryUrl() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->registryUrl;
}

const std::string &Node::lastError() const
{
    return impl_->lastError;
}

std::vector<std::string> Node::instances(const std::string &typeName) const
{
    std::vector<std::string> result;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto &entry : impl_->registryTypes) {
        if (typeName.empty() || entry.second == typeName)
            result.push_back(entry.first);
    }
    return result;
}

void Node::addReplica(std::unique_ptr<Replica> replica)
{
    Replica *raw = replica.get();
    std::string url = raw->hostUrl();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->replicas.push_back(std::move(replica));
        if (url.empty() && !raw->name().empty()) {
            auto entry = impl_->registrySources.find(raw->name());
            if (entry != impl_->registrySources.end())
                url = entry->second;
        }
        if (url.empty())
            url = impl_->defaultUrl;
    }
    if (url.empty())
        return;  // wait for a registry entry
    if (raw->hostUrl().empty())
        raw->setHostUrl(url);
    impl_->bindReplica(raw);
}

PendingCall Node::invoke(const Replica &replica, uint16_t method, const Value::List &args)
{
    auto state = std::make_shared<PendingCall::State>();
    const std::string url = replica.hostUrl();

    bool connected = false;
    uint64_t serial = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto it = impl_->connections.find(url);
        connected = it != impl_->connections.end() && it->second->isConnected();
        if (connected) {
            serial = impl_->nextSerial++;
            impl_->pending[serial] = state;
        }
    }
    if (!connected) {
        RO_WARN("node: cannot invoke '%s', not connected to '%s'", replica.name().c_str(),
                url.c_str());
        std::lock_guard<std::mutex> lock(state->mutex);
        state->finished = true;
        state->error = PendingCall::Error::UnknownMethod;
        return PendingCall(state);
    }

    impl_->sendTo(url, PacketType::Invoke,
                  packets::invoke(replica.name(), method, args, static_cast<int64_t>(serial)));
    return PendingCall(state);
}

}  // namespace ro
