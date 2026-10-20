// machine server: runs a simulated machine that clients can drive remotely.
#include <chrono>
#include <cstdio>
#include <thread>

#include "rep_machine.h"
#include "ro/host.h"

namespace {

class MachineControl : public MachineSimpleSource {
public:
    void start(int32_t cycles) override
    {
        target_ = cycles;
        setCycles(0);
        setState(State::Running);
        emitStateTransition(State::Running);
        std::printf("start(%d)\n", cycles);
    }

    void stop() override
    {
        setState(State::Idle);
        emitStateTransition(State::Idle);
        std::printf("stop()\n");
    }

    Reading lastReading() override { return last(); }

    // Called from the main loop; emits one simulated cycle.
    void tick()
    {
        Reading reading;
        reading.sensor = cycles() + 1;
        reading.value = 10.0 + reading.sensor * 0.5;
        setLast(reading);
        setCycles(reading.sensor);
        emitCycleFinished(reading, cycles());

        if (cycles() >= target_) {
            setState(State::Idle);
            emitStateTransition(State::Idle);
            std::printf("finished %d cycles\n", cycles());
        }
    }

    bool running() const { return state() == State::Running; }

private:
    int32_t target_ = 0;
};

}  // namespace

int main(int argc, char **argv)
{
    const std::string address = argc > 1 ? argv[1] : "tcp://127.0.0.1:4344";

    MachineControl source;
    ro::Host host;
    if (!host.listen(address)) {
        std::printf("listen failed: %s\n", host.lastError().c_str());
        return 1;
    }
    host.enableRemoting(&source, "Machine");
    std::printf("host listening on %s\n", host.url().c_str());

    for (int i = 0; i < 120; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (source.running())
            source.tick();
    }
    return 0;
}
