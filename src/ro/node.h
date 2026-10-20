// remoteobject - client node: creates replicas and talks to hosts.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "pendingcall.h"
#include "replica.h"

namespace ro {

class RegistryReplica;

// A Node owns all replicas created through it and keeps their connections.
class Node {
public:
    Node();
    ~Node();

    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;

    // "tcp://host:port" or "ws://host:port". Returns false on a malformed
    // address.
    bool connectToNode(const std::string &address);
    // Objects hosted by registry peers become discoverable by name.
    bool setRegistryUrl(const std::string &address);
    bool waitForRegistry(int timeoutMs = 30000);
    bool waitForConnected(int timeoutMs = 30000);

    std::string registryUrl() const;
    const std::string &lastError() const;

    // Creates a replica; ownership stays with the node.
    template <class T> T *acquire(std::string name = {})
    {
        static_assert(std::is_base_of_v<Replica, T>, "acquire<T>: T must derive from ro::Replica");
        T *replica = new T(this, std::move(name));
        addReplica(std::unique_ptr<Replica>(replica));
        return replica;
    }

    // Remote names known through the registry, filtered by type (empty = all).
    std::vector<std::string> instances(const std::string &typeName = {}) const;

    // Fire a remote method call.
    PendingCall invoke(const Replica &replica, uint16_t method, const Value::List &args);

    Signal<std::string, std::string> remoteObjectAdded;  // name, type
    Signal<std::string> remoteObjectRemoved;             // name

private:
    friend class RegistryReplica;

    struct Impl;
    std::unique_ptr<Impl> impl_;

    void addReplica(std::unique_ptr<Replica> replica);
};

}  // namespace ro
