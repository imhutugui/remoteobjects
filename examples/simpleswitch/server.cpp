// simpleswitch server: publishes a switch and toggles it once per second.
#include <chrono>
#include <cstdio>
#include <thread>

#include "rep_simpleswitch.h"
#include "ro/host.h"

namespace {

class Switch : public SimpleSwitchSimpleSource {
public:
    // Called on the IO thread when a client invokes the slot.
    void server_slot(bool clientState) override
    {
        std::printf("server_slot(%s)\n", clientState ? "true" : "false");
        setCurrState(clientState);
    }
};

}  // namespace

int main(int argc, char **argv)
{
    const std::string address = argc > 1 ? argv[1] : "tcp://127.0.0.1:4343";

    Switch source;
    ro::Host host;
    if (!host.listen(address)) {
        std::printf("listen failed: %s\n", host.lastError().c_str());
        return 1;
    }
    if (!host.enableRemoting(&source, "SimpleSwitch")) {
        std::printf("enableRemoting failed: %s\n", host.lastError().c_str());
        return 1;
    }
    std::printf("host listening on %s\n", host.url().c_str());

    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        source.setCurrState(!source.currState());
        std::printf("toggled currState to %s\n", source.currState() ? "true" : "false");
    }
    return 0;
}
