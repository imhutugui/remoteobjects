// remoteobject - asynchronous result of a remote method call.
#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>

#include "value.h"

namespace ro {

class Node;

class PendingCall {
public:
    enum class Error { NoError = 0, UnknownMethod, Timeout };

    PendingCall() = default;

    bool isValid() const { return state_ != nullptr; }
    bool isFinished() const;
    // Returns false on timeout; check error() afterwards.
    bool waitForFinished(int timeoutMs = 30000);
    Value returnValue() const;
    Error error() const;

private:
    friend class Node;

    struct State {
        mutable std::mutex mutex;
        std::condition_variable cv;
        bool finished = false;
        Value value;
        Error error = Error::NoError;
    };

    explicit PendingCall(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};

}  // namespace ro
