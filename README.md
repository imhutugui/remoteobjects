# remoteobject

A Qt-free reimplementation of the core ideas behind Qt Remote Objects
(`qtremoteobjects`). Same shape — `.rep` interface files, generated Source and
Replica classes, a host/node split, a shared registry — but built on plain
C++20, [xmake](https://xmake.io) and [asio2](https://github.com/zhllxt/asio2).

`remoteobject` is **not wire compatible** with Qt: the protocol, the code
generator and the object model are its own. It is a reimplementation of the
concept, not a port of the library.

```
 .rep file ──repgen──> rep_<name>.h ──> <Name>SimpleSource  (server side)
                                    └─> <Name>Replica       (client side)
```

## Requirements

* xmake 3.x (dependencies are fetched from xrepo, no manual installs)
* A C++20 compiler

`asio2` is pulled in with `add_requires("asio2")` — `xmake` will install it on
the first build.

## Build, test, run

```sh
xmake                 # build the library (libro.a) and repgen
xmake build ro_tests  # build the test binary
./build/macosx/arm64/debug/ro_tests   # 29 tests

xmake build simpleswitch_server
xmake build simpleswitch_client
```

Two terminals for the examples:

```sh
./build/macosx/arm64/debug/simpleswitch_server   # listens on tcp://127.0.0.1:4343
./build/macosx/arm64/debug/simpleswitch_client
```

The address argument selects the transport — the same pair works over
WebSocket:

```sh
./build/macosx/arm64/debug/simpleswitch_server ws://127.0.0.1:4443
./build/macosx/arm64/debug/simpleswitch_client ws://127.0.0.1:4443
```

`examples/machine` exercises the rest of the feature set: enums, PODs,
multi-argument signals and a slot with a return value.

## Quick start

`switch.rep`:

```
class SimpleSwitch
{
    PROP(bool currState=false);
    SLOT(void server_slot(bool clientState));
};
```

Generate the header (checked in, regenerate after editing the `.rep`):

```sh
xmake run repgen switch.rep -o rep_switch.h
```

Server:

```cpp
#include "rep_switch.h"
#include "ro/host.h"

class Switch : public SimpleSwitchSimpleSource {
public:
    void server_slot(bool state) override { setCurrState(state); }
};

int main() {
    Switch source;
    ro::Host host;
    host.listen("tcp://127.0.0.1:4343");
    host.enableRemoting(&source, "SimpleSwitch");   // name defaults to the type
    std::this_thread::sleep_for(std::chrono::hours(1));
}
```

Client:

```cpp
#include "rep_switch.h"
#include "ro/node.h"

int main() {
    ro::Node node;
    node.connectToNode("tcp://127.0.0.1:4343");

    auto *replica = node.acquire<SimpleSwitchReplica>("SimpleSwitch");
    replica->currStateChanged.connect([](bool state) { /* ... */ });
    replica->waitForSource(5000);

    replica->server_slot(true).waitForFinished(5000);   // remote call
}
```

## `.rep` support

| Feature | Status |
| --- | --- |
| `class`, `PROP`, `SIGNAL`, `SLOT` | yes |
| `PROP` modifiers (`READONLY`, `CONSTANT`, `READPUSH`, `SOURCEONLYSETTER`) | parsed; `READONLY`/`CONSTANT` get no setter |
| Property default values | yes |
| `ENUM` (top level and inside a class) | yes |
| `POD` structs, including nesting and containers of PODs | yes |
| `QList`/`QVector`/`std::vector`, `QMap`/`QHash`/`std::map` | yes |
| `USE_ENUM`, `FLAG`, `#include` directives | ignored |
| `MODEL` (`QAbstractItemModel`) | rejected with a clear error |
| `CLASS(x)` child replication | rejected with a clear error |

Type mapping: `int → int32_t`, `uint → uint32_t`, `qlonglong → int64_t`,
`float/double`, `QString → std::string`, `QByteArray → std::string`,
`QVariant → ro::Value`. Anything else (e.g. `QTime`, `QDate`) is a codegen
error rather than a silent miss.

## QtRO → remoteobject

| Qt Remote Objects | remoteobject |
| --- | --- |
| `QRemoteObjectHost::enableRemoting(obj, name)` | `ro::Host::enableRemoting(source, name)` |
| `QRemoteObjectNode::connectToNode(url)` | `ro::Node::connectToNode("tcp://host:port")` or `("ws://host:port")` |
| `node.acquire<T>(name)` | `node.acquire<T>(name)`, same ownership rule (node owns) |
| `QRemoteObjectReplica::State` | `ro::Replica::State` (same five states) |
| `replica->initialized()` / `stateChanged` | `ro::Signal<> initialized` / `stateChanged` |
| slot call returning `QRemoteObjectPendingCall` | `ro::PendingCall` + `waitForFinished()` |
| `QRemoteObjectRegistryHost` | `ro::RegistryHost` |
| `QRemoteObjectHost::setRegistryUrl` | `ro::Host::setRegistryUrl` |
| `Q_CLASSINFO("RemoteObject Signature")` checks | signature hash compared in `Init` |

Signals are `ro::Signal<Args...>`: `.connect(fn)` returns an id, `.disconnect(id)`.

## Design

**Wire protocol.** Frames are `[u32 length][u16 type][body]`, little endian,
varint integers, self-describing values (`String`, `Bytes`, `List`, `Map`,
`Struct`) so the receiver can decode without the source's C++ types. Packet
types: `Handshake`, `Acquire`, `Init`, `RemoveObject`, `Invoke`, `InvokeReply`,
`Signal`, `PropertyChange`, `Ping`, `Pong`, `Error`, `RegistryAnnounce`,
`RegistryRemove`.

**Transports.** The protocol is transport independent: a URL scheme picks the
carrier. `tcp://host:port` speaks raw TCP; `ws://host:port` speaks WebSocket
(served by asio2) and delivers the same frames as WebSocket binary messages.
`Host::listen`, `Node::connectToNode` and both `setRegistryUrl` entry points
accept either scheme, so a node may talk to one host over TCP and another over
WebSocket. Any standard WebSocket client (including a browser page) can speak
the protocol too, as long as it implements the frame format from
`src/ro/protocol.*`. The transport layer lives behind
`ro::ClientTransport`/`ro::ServerTransport` (`src/ro/transport.*`).
`wss://` (TLS) is not implemented.

**Type safety.** `repgen` hashes the canonical form of the interface into a
signature embedded in both generated classes. A replica whose signature does
not match the source's `Init` packet goes to `SignatureMismatch` and stays
there instead of silently decoding garbage.

**Threading.** Host and node each run asio2's IO context on its own thread.
Source state and replica caches are mutex protected, so user code may set
properties and read replica values from any thread. Signals and slot handlers
run on the IO thread — do not block them.

**States.** `Uninitialized → Valid` on the first `Init`; `Valid → Suspect` when
the connection drops or the source is unregistered; `Suspect → Valid` after a
reconnect, because the node re-sends `Acquire` and the host answers with a fresh
`Init`. Reconnection is automatic (asio2 `set_auto_reconnect`, 1 s delay).

**Registry.** `RegistryHost` serves a built-in remote object (`rl.registry`)
that maps object names to host URLs. Hosts announce themselves through
`Host::setRegistryUrl`; nodes discover names with `Node::setRegistryUrl` and
`Node::instances()`. A registry entry lets a node open a second connection to
the host that actually owns the object.

## Limitations (v0.1)

* No `MODEL`/`QAbstractItemModel` replication, no child replication.
* No SSL/TLS: `wss://` is rejected (clear error), only plain `tcp://` and
  `ws://` — no Unix domain sockets, no `local:` scheme.
* No dynamic replicas (`acquireDynamic`), no QML bindings, no `.rep` namespaces.
* `Ping`/`Pong` are implemented and answered, but no periodic heartbeat is
  scheduled; dead peers are detected by the connection dropping.
* Persisted properties (`QRemoteObjectAbstractPersistedStore`) are not
  implemented.
* Pending calls have no timeout timer: `waitForFinished(ms)` returns `false` and
  sets `PendingCall::Error::Timeout`, but the call is not cancelled remotely.

## Layout

```
src/ro/          runtime library
  value.*        dynamic Value + binary codec
  wire.*         little endian reader/writer
  protocol.*     packet types and framing
  transport.*    TCP / WebSocket carrier (ro::ClientTransport, ro::ServerTransport)
  address.*      URL parsing ("tcp://", "ws://")
  signal.h       Signal<Args...> with optional remote fan-out
  source.*       SourceBase (generated sources derive from this)
  replica.*      Replica state machine and property cache
  node.*         Node: connections, replicas, pending calls, registry client
  host.*         Host: server (tcp/ws), subscriptions, property/signal fan-out
  registry.*     RegistrySource / RegistryReplica / RegistryHost
tools/repgen/    .rep lexer, parser, AST, C++ header generator
tests/           unit + end-to-end tests (in-process TCP and WebSocket, registry, reconnect)
examples/        simpleswitch, machine, robot
scripts/gen.sh   regenerate rep_*.h from every examples/*/*.rep
```

## Licence

This directory contains new code with no licence header yet — pick one before
publishing it outside this repository.
