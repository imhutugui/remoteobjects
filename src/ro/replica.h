// remoteobject - client side handle for a remote object.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "signal.h"
#include "value.h"

namespace ro {

class Node;

class Replica {
public:
    // Mirrors QRemoteObjectReplica::State.
    enum class State { Uninitialized, Default, Valid, Suspect, SignatureMismatch };
    static const char *stateName(State state);

    Replica(Node *node, std::string name, std::string typeName, std::string signature);
    virtual ~Replica();

    const std::string &name() const { return name_; }
    const std::string &typeName() const { return typeName_; }
    const std::string &signature() const { return signature_; }

    State state() const;
    bool isReplicaValid() const { return state() == State::Valid; }
    bool isInitialized() const { return state() == State::Valid; }

    // Blocks until the replica is initialized (or failed).
    bool waitForSource(int timeoutMs = 30000);

    uint16_t propertyCount() const { return static_cast<uint16_t>(properties_.size()); }
    uint16_t signalCount() const { return static_cast<uint16_t>(signals_.size()); }

    Signal<State, State> stateChanged;
    Signal<> initialized;

    // Connection URL this replica is attached to; empty while unresolved.
    std::string hostUrl() const;
    void setHostUrl(std::string url);

    void setState(State state);

    // Called by Node on the IO thread.
    void handleInit(const std::string &typeName, const std::string &signature,
                    const Value::Map &properties);
    void handlePropertyChange(uint16_t index, const Value &value);
    void handleSignal(uint16_t index, const Value::List &args);
    void handleSourceRemoved();

protected:
    // Used by generated subclasses, in declaration order.
    void registerProperty(std::string name, std::function<void(const Value &)> apply);
    void registerSignal(std::function<void(const Value::List &)> dispatch);

    Node *node() const { return node_; }

private:
    Node *node_;
    std::string name_;
    std::string typeName_;
    std::string signature_;
    std::vector<std::pair<std::string, std::function<void(const Value &)>>> properties_;
    std::vector<std::function<void(const Value::List &)>> signals_;

    mutable std::mutex hostUrlMutex_;
    std::string hostUrl_;

    mutable std::mutex stateMutex_;
    std::condition_variable stateCv_;
    State state_ = State::Uninitialized;
};

}  // namespace ro
