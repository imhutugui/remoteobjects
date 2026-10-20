// robot server: a simulated differential-drive robot. It integrates the
// commanded linear/angular velocity at a fixed rate and pushes every pose
// change to connected replicas.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "rep_robot.h"
#include "ro/host.h"

namespace {

constexpr double kTickSeconds = 0.05;  // 20 Hz integration

class RobotControl : public RobotSimpleSource {
public:
    void move(double linear, double angular) override
    {
        linear_ = linear;
        angular_ = angular;
        std::printf("move(linear=%.2f, angular=%.2f)\n", linear, angular);
    }

    void stop() override
    {
        linear_ = 0.0;
        angular_ = 0.0;
        std::printf("stop()\n");
    }

    // One integration step: advance the pose, push the new values. Nothing to
    // do while the robot is standing still, so replicas are not spammed with
    // unchanged values.
    void tick()
    {
        if (linear_ == 0.0 && angular_ == 0.0)
            return;
        const double dt = kTickSeconds;
        const double theta = this->theta();
        setX(x() + linear_ * std::cos(theta) * dt);
        setY(y() + linear_ * std::sin(theta) * dt);
        setTheta(theta + angular_ * dt);
    }

private:
    double linear_ = 0.0;
    double angular_ = 0.0;
};

}  // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // unbuffered: slot logs survive SIGTERM
    const std::string address = argc > 1 ? argv[1] : "tcp://127.0.0.1:4345";

    RobotControl source;
    ro::Host host;
    if (!host.listen(address)) {
        std::printf("listen failed: %s\n", host.lastError().c_str());
        return 1;
    }
    host.enableRemoting(&source, "Robot");
    std::printf("host listening on %s\n", host.url().c_str());

    const auto tick = std::chrono::milliseconds(static_cast<int>(kTickSeconds * 1000));
    for (int i = 0; i < 1200; ++i) {  // 60 s, then exit
        std::this_thread::sleep_for(tick);
        source.tick();
    }
    return 0;
}
