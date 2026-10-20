#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

#include "rep_simpleswitch.h"
#include "ro/host.h"
#include "ro/node.h"
#include "ro/registry.h"
#include "test.h"

using namespace ro;

namespace {

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 4000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}

class SwitchSource : public SimpleSwitchSimpleSource {
public:
    void server_slot(bool clientState) override
    {
        lastFromClient.store(clientState);
        ++slotCalls;
        setCurrState(clientState);
    }

    std::atomic<bool> lastFromClient{false};
    std::atomic<int> slotCalls{0};
};

// Binds the first free port in [start, start+20) and returns its URL.
std::string listenOnFreePort(Host &host, uint16_t start, const std::string &scheme = "tcp")
{
    for (uint16_t port = start; port < start + 20; ++port) {
        const std::string url = scheme + "://127.0.0.1:" + std::to_string(port);
        if (host.listen(url))
            return url;
    }
    return {};
}

class WrongSignatureReplica : public Replica {
public:
    explicit WrongSignatureReplica(Node *node, std::string name)
        : Replica(node, std::move(name), "SimpleSwitch", "not-the-right-signature")
    {
    }
};

}  // namespace

RO_TEST(e2e_initial_properties_arrive_on_acquire)
{
    SwitchSource source;
    source.setCurrState(true);

    Host host;
    const std::string url = listenOnFreePort(host, 28100);
    RO_CHECK(!url.empty());
    RO_CHECK(host.enableRemoting(&source, "SimpleSwitch"));

    Node node;
    RO_CHECK(node.connectToNode(url));
    auto *replica = node.acquire<SimpleSwitchReplica>("SimpleSwitch");
    RO_CHECK(replica->waitForSource(4000));
    RO_CHECK(replica->isReplicaValid());
    RO_CHECK_EQ(replica->currState(), true);
}

RO_TEST(e2e_property_change_pushes_to_replica)
{
    SwitchSource source;
    Host host;
    const std::string url = listenOnFreePort(host, 28120);
    RO_CHECK(!url.empty());
    RO_CHECK(host.enableRemoting(&source, "SimpleSwitch"));

    Node node;
    RO_CHECK(node.connectToNode(url));
    auto *replica = node.acquire<SimpleSwitchReplica>("SimpleSwitch");
    RO_CHECK(replica->waitForSource(4000));

    std::atomic<int> changes{0};
    std::atomic<bool> lastValue{false};
    replica->currStateChanged.connect([&](bool value) {
        lastValue.store(value);
        ++changes;
    });

    source.setCurrState(true);
    RO_CHECK(waitFor([&] { return changes.load() == 1; }));
    RO_CHECK_EQ(lastValue.load(), true);
    RO_CHECK_EQ(replica->currState(), true);

    source.setCurrState(false);
    RO_CHECK(waitFor([&] { return changes.load() == 2; }));
    RO_CHECK_EQ(lastValue.load(), false);
}

RO_TEST(e2e_slot_call_reaches_source_and_returns)
{
    SwitchSource source;
    Host host;
    const std::string url = listenOnFreePort(host, 28140);
    RO_CHECK(!url.empty());
    RO_CHECK(host.enableRemoting(&source, "SimpleSwitch"));

    Node node;
    RO_CHECK(node.connectToNode(url));
    auto *replica = node.acquire<SimpleSwitchReplica>("SimpleSwitch");
    RO_CHECK(replica->waitForSource(4000));

    PendingCall call = replica->server_slot(true);
    RO_CHECK(call.waitForFinished(4000));
    RO_CHECK_EQ(static_cast<int>(call.error()), static_cast<int>(PendingCall::Error::NoError));
    RO_CHECK_EQ(source.slotCalls.load(), 1);
    RO_CHECK_EQ(source.lastFromClient.load(), true);

    // The slot changed the property, which must reach the replica.
    RO_CHECK(waitFor([&] { return replica->currState(); }));
}

RO_TEST(e2e_two_nodes_share_one_source)
{
    SwitchSource source;
    Host host;
    const std::string url = listenOnFreePort(host, 28160);
    RO_CHECK(!url.empty());
    RO_CHECK(host.enableRemoting(&source, "SimpleSwitch"));

    Node first;
    Node second;
    RO_CHECK(first.connectToNode(url));
    RO_CHECK(second.connectToNode(url));
    auto *a = first.acquire<SimpleSwitchReplica>("SimpleSwitch");
    auto *b = second.acquire<SimpleSwitchReplica>("SimpleSwitch");
    RO_CHECK(a->waitForSource(4000));
    RO_CHECK(b->waitForSource(4000));

    source.setCurrState(true);
    RO_CHECK(waitFor([&] { return a->currState() && b->currState(); }));
}

RO_TEST(e2e_wrong_signature_is_rejected)
{
    SwitchSource source;
    Host host;
    const std::string url = listenOnFreePort(host, 28180);
    RO_CHECK(!url.empty());
    RO_CHECK(host.enableRemoting(&source, "SimpleSwitch"));

    Node node;
    RO_CHECK(node.connectToNode(url));
    auto *replica = node.acquire<WrongSignatureReplica>("SimpleSwitch");
    RO_CHECK(waitFor([&] {
        return replica->state() == Replica::State::SignatureMismatch;
    }));
}

RO_TEST(e2e_registry_host_discovers_local_sources)
{
    RegistryHost registry;
    const std::string registryUrl = listenOnFreePort(registry, 28200);
    RO_CHECK(!registryUrl.empty());

    SwitchSource source;
    RO_CHECK(registry.enableRemoting(&source, "SwitchA"));

    Node node;
    RO_CHECK(node.setRegistryUrl(registryUrl));
    RO_CHECK(node.waitForRegistry(4000));
    RO_CHECK(waitFor([&] { return node.instances("SimpleSwitch").size() == 1; }));
    const std::vector<std::string> names = node.instances("SimpleSwitch");
    RO_CHECK_EQ(names.size(), size_t{1});
    RO_CHECK_EQ(names[0], std::string("SwitchA"));

    auto *replica = node.acquire<SimpleSwitchReplica>("SwitchA");
    RO_CHECK(replica->waitForSource(4000));
    RO_CHECK(replica->isReplicaValid());
}

RO_TEST(e2e_remote_host_announces_to_registry)
{
    RegistryHost registry;
    const std::string registryUrl = listenOnFreePort(registry, 28220);
    RO_CHECK(!registryUrl.empty());

    SwitchSource source;
    Host worker;
    const std::string workerUrl = listenOnFreePort(worker, 28240);
    RO_CHECK(!workerUrl.empty());
    RO_CHECK(worker.setRegistryUrl(registryUrl));
    RO_CHECK(worker.enableRemoting(&source, "SwitchB"));

    Node node;
    RO_CHECK(node.setRegistryUrl(registryUrl));
    RO_CHECK(node.waitForRegistry(4000));
    RO_CHECK(waitFor([&] { return node.instances("SimpleSwitch").size() == 1; }));

    auto *replica = node.acquire<SimpleSwitchReplica>("SwitchB");
    RO_CHECK(replica->waitForSource(5000));
    RO_CHECK_EQ(replica->hostUrl(), workerUrl);

    source.setCurrState(true);
    RO_CHECK(waitFor([&] { return replica->currState(); }));
}

RO_TEST(e2e_reconnects_after_host_restart)
{
    SwitchSource source;
    Host host;
    const std::string url = listenOnFreePort(host, 28280);
    RO_CHECK(!url.empty());
    RO_CHECK(host.enableRemoting(&source, "SimpleSwitch"));

    Node node;
    RO_CHECK(node.connectToNode(url));
    auto *replica = node.acquire<SimpleSwitchReplica>("SimpleSwitch");
    RO_CHECK(replica->waitForSource(4000));

    host.stop();
    RO_CHECK(waitFor([&] { return replica->state() == Replica::State::Suspect; }));

    Host restarted;
    RO_CHECK(restarted.listen(url));
    RO_CHECK(restarted.enableRemoting(&source, "SimpleSwitch"));
    RO_CHECK(waitFor([&] { return replica->state() == Replica::State::Valid; }, 8000));

    source.setCurrState(true);
    RO_CHECK(waitFor([&] { return replica->currState(); }));
}

RO_TEST(e2e_disable_remoting_marks_replica_suspect)
{
    SwitchSource source;
    Host host;
    const std::string url = listenOnFreePort(host, 28260);
    RO_CHECK(!url.empty());
    RO_CHECK(host.enableRemoting(&source, "SimpleSwitch"));

    Node node;
    RO_CHECK(node.connectToNode(url));
    auto *replica = node.acquire<SimpleSwitchReplica>("SimpleSwitch");
    RO_CHECK(replica->waitForSource(4000));

    RO_CHECK(host.disableRemoting(&source));
    RO_CHECK(waitFor([&] { return replica->state() == Replica::State::Suspect; }));
}

// The full e2e flow over a WebSocket transport instead of raw TCP:
// property init on acquire, property change fan-out, and a slot round-trip.
RO_TEST(e2e_websocket_transport_full_flow)
{
    SwitchSource source;
    source.setCurrState(true);

    Host host;
    const std::string url = listenOnFreePort(host, 28300, "ws");
    RO_CHECK(!url.empty());
    RO_CHECK(url.rfind("ws://127.0.0.1:", 0) == 0);
    RO_CHECK(host.enableRemoting(&source, "SimpleSwitch"));

    Node node;
    RO_CHECK(node.connectToNode(url));
    RO_CHECK(node.waitForConnected(4000));
    auto *replica = node.acquire<SimpleSwitchReplica>("SimpleSwitch");
    RO_CHECK(replica->waitForSource(4000));
    RO_CHECK(replica->isReplicaValid());
    RO_CHECK_EQ(replica->currState(), true);

    std::atomic<int> changes{0};
    replica->currStateChanged.connect([&](bool value) {
        (void)value;
        ++changes;
    });
    source.setCurrState(false);
    RO_CHECK(waitFor([&] { return changes.load() == 1; }));
    RO_CHECK_EQ(replica->currState(), false);

    PendingCall call = replica->server_slot(true);
    RO_CHECK(call.waitForFinished(4000));
    RO_CHECK_EQ(static_cast<int>(call.error()), static_cast<int>(PendingCall::Error::NoError));
    RO_CHECK_EQ(source.slotCalls.load(), 1);
}
