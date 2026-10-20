// robot client: drives the remote robot and prints every pose change that
// the replica receives.
#include <chrono>
#include <cstdio>
#include <thread>

#include "rep_robot.h"
#include "ro/node.h"

int main(int argc, char **argv)
{
    const std::string address = argc > 1 ? argv[1] : "tcp://127.0.0.1:4345";

    ro::Node node;
    if (!node.connectToNode(address)) {
        std::printf("connect failed: %s\n", node.lastError().c_str());
        return 1;
    }

    auto *replica = node.acquire<RobotReplica>("Robot");
    replica->xChanged.connect([](double x) { std::printf("x=%.3f\n", x); });
    replica->yChanged.connect([](double y) { std::printf("y=%.3f\n", y); });
    replica->thetaChanged.connect([](double theta) { std::printf("theta=%.3f\n", theta); });

    if (!replica->waitForSource(5000)) {
        std::printf("source never became available\n");
        return 1;
    }
    std::printf("acquired 'Robot' at x=%.3f y=%.3f theta=%.3f\n", replica->x(), replica->y(),
                replica->theta());

    // Drive forward while turning, for four seconds.
    replica->move(0.5, 0.3).waitForFinished(5000);
    std::this_thread::sleep_for(std::chrono::seconds(4));

    // Stop, then read the final pose back over a slot-free property read.
    replica->stop().waitForFinished(5000);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::printf("final pose: x=%.3f y=%.3f theta=%.3f\n", replica->x(), replica->y(),
                replica->theta());
    return 0;
}
