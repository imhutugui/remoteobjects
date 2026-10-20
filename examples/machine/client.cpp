// machine client: drives the remote machine and prints its signals.
#include <chrono>
#include <cstdio>
#include <thread>

#include "rep_machine.h"
#include "ro/node.h"

int main(int argc, char **argv)
{
    const std::string address = argc > 1 ? argv[1] : "tcp://127.0.0.1:4344";

    ro::Node node;
    if (!node.connectToNode(address)) {
        std::printf("connect failed: %s\n", node.lastError().c_str());
        return 1;
    }

    auto *replica = node.acquire<MachineReplica>("Machine");
    replica->cycleFinished.connect([](Reading reading, int32_t total) {
        std::printf("cycleFinished(sensor=%d value=%.1f total=%d)\n", reading.sensor, reading.value,
                    total);
    });
    replica->stateTransition.connect([](MachineReplica::State state) {
        std::printf("stateTransition(%s)\n",
                    state == MachineReplica::State::Running ? "Running" : "Idle");
    });
    replica->cyclesChanged.connect([](int32_t cycles) { std::printf("cycles=%d\n", cycles); });

    if (!replica->waitForSource(5000)) {
        std::printf("source never became available\n");
        return 1;
    }
    std::printf("acquired 'Machine', state=%d cycles=%d\n", static_cast<int>(replica->state()),
                replica->cycles());

    replica->start(3).waitForFinished(5000);

    // Wait until the machine reports that it is idle again.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline
           && replica->state() != MachineReplica::State::Idle) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ro::PendingCall call = replica->lastReading();
    if (call.waitForFinished(3000)) {
        const Reading reading = ro::fromValue<Reading>(call.returnValue());
        std::printf("lastReading(): sensor=%d value=%.1f\n", reading.sensor, reading.value);
    } else {
        std::printf("lastReading() timed out\n");
    }

    replica->stop().waitForFinished(3000);
    return 0;
}
