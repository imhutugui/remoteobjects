#include "pendingcall.h"

namespace ro {

bool PendingCall::isFinished() const
{
    if (!state_)
        return true;
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->finished;
}

bool PendingCall::waitForFinished(int timeoutMs)
{
    if (!state_)
        return true;
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (!state_->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                             [this] { return state_->finished; })) {
        state_->error = Error::Timeout;
        return false;
    }
    return true;
}

Value PendingCall::returnValue() const
{
    if (!state_)
        return Value();
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->value;
}

PendingCall::Error PendingCall::error() const
{
    if (!state_)
        return Error::UnknownMethod;
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->error;
}

}  // namespace ro
