#include "replica.h"

#include "log.h"

namespace ro {

const char *Replica::stateName(State state)
{
    switch (state) {
    case State::Uninitialized: return "Uninitialized";
    case State::Default: return "Default";
    case State::Valid: return "Valid";
    case State::Suspect: return "Suspect";
    case State::SignatureMismatch: return "SignatureMismatch";
    }
    return "Unknown";
}

Replica::Replica(Node *node, std::string name, std::string typeName, std::string signature)
    : node_(node), name_(std::move(name)), typeName_(std::move(typeName)),
      signature_(std::move(signature))
{
}

Replica::~Replica() = default;

Replica::State Replica::state() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

bool Replica::waitForSource(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(stateMutex_);
    const auto ready = [this] {
        return state_ == State::Valid || state_ == State::SignatureMismatch;
    };
    if (ready())
        return state_ == State::Valid;
    return stateCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready)
           && state_ == State::Valid;
}

std::string Replica::hostUrl() const
{
    std::lock_guard<std::mutex> lock(hostUrlMutex_);
    return hostUrl_;
}

void Replica::setHostUrl(std::string url)
{
    std::lock_guard<std::mutex> lock(hostUrlMutex_);
    hostUrl_ = std::move(url);
}

void Replica::registerProperty(std::string name, std::function<void(const Value &)> apply)
{
    properties_.emplace_back(std::move(name), std::move(apply));
}

void Replica::registerSignal(std::function<void(const Value::List &)> dispatch)
{
    signals_.push_back(std::move(dispatch));
}

void Replica::setState(State state)
{
    State old;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (state_ == state)
            return;
        old = state_;
        state_ = state;
    }
    stateCv_.notify_all();
    stateChanged.emit(old, state);
}

void Replica::handleInit(const std::string &typeName, const std::string &signature,
                         const Value::Map &properties)
{
    if (signature != signature_) {
        RO_ERROR("replica '%s': signature mismatch (local %s, remote %s)", name_.c_str(),
                 signature_.c_str(), signature.c_str());
        setState(State::SignatureMismatch);
        return;
    }
    if (typeName != typeName_)
        RO_WARN("replica '%s': remote type is '%s', expected '%s'", name_.c_str(),
                typeName.c_str(), typeName_.c_str());

    for (const auto &entry : properties) {
        const std::string &propertyName = entry.first.str();
        bool found = false;
        for (size_t i = 0; i < properties_.size(); ++i) {
            if (properties_[i].first == propertyName) {
                properties_[i].second(entry.second);
                found = true;
                break;
            }
        }
        if (!found)
            RO_WARN("replica '%s': unknown property '%s' in Init packet", name_.c_str(),
                    propertyName.c_str());
    }

    setState(State::Valid);
    initialized.emit();
}

void Replica::handlePropertyChange(uint16_t index, const Value &value)
{
    if (index >= properties_.size()) {
        RO_WARN("replica '%s': property index %u out of range", name_.c_str(), index);
        return;
    }
    properties_[index].second(value);
}

void Replica::handleSignal(uint16_t index, const Value::List &args)
{
    if (index >= signals_.size()) {
        RO_WARN("replica '%s': signal index %u out of range", name_.c_str(), index);
        return;
    }
    signals_[index](args);
}

void Replica::handleSourceRemoved()
{
    if (state() == State::Valid)
        setState(State::Suspect);
}

}  // namespace ro
