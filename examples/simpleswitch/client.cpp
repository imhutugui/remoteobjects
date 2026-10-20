// simpleswitch client: acquires the replica and reacts to property changes.
#include <chrono>
#include <cstdio>
#include <thread>

#include "rep_simpleswitch.h"
#include "ro/node.h"

int main(int argc, char **argv)
{
    const std::string address = argc > 1 ? argv[1] : "tcp://127.0.0.1:4343";

    ro::Node node;
    if (!node.connectToNode(address)) {
        std::printf("connect failed: %s\n", node.lastError().c_str());
        return 1;
    }

    auto *replica = node.acquire<SimpleSwitchReplica>("SimpleSwitch");
    replica->currStateChanged.connect(
        [](bool state) { std::printf("currStateChanged(%s)\n", state ? "true" : "false"); });
    replica->stateChanged.connect([](ro::Replica::State from, ro::Replica::State to) {
        std::printf("replica state: %s -> %s\n", ro::Replica::stateName(from),
                    ro::Replica::stateName(to));
    });

    if (!replica->waitForSource(5000)) {
        std::printf("source never became available\n");
        return 1;
    }
    std::printf("acquired 'SimpleSwitch', currState=%s\n", replica->currState() ? "true" : "false");

    // Flip the switch remotely and wait for the server to acknowledge.
    ro::PendingCall call = replica->server_slot(!replica->currState());
    if (!call.waitForFinished(5000)) {
        std::printf("slot call timed out\n");
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));
    return 0;
}
