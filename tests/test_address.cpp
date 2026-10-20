#include "ro/address.h"
#include "test.h"

using namespace ro;

RO_TEST(address_parses_tcp_url)
{
    Address a;
    std::string error;
    RO_CHECK(parseAddress("tcp://127.0.0.1:4343", a, error));
    RO_CHECK(a.transport == Transport::Tcp);
    RO_CHECK_EQ(a.host, std::string("127.0.0.1"));
    RO_CHECK_EQ(a.port, uint16_t{4343});
    RO_CHECK_EQ(a.url, std::string("tcp://127.0.0.1:4343"));
}

RO_TEST(address_parses_websocket_url)
{
    Address a;
    std::string error;
    RO_CHECK(parseAddress("ws://example.com:8080", a, error));
    RO_CHECK(a.transport == Transport::Websocket);
    RO_CHECK_EQ(a.host, std::string("example.com"));
    RO_CHECK_EQ(a.port, uint16_t{8080});
    RO_CHECK_EQ(a.url, std::string("ws://example.com:8080"));
}

RO_TEST(address_bare_host_port_defaults_to_tcp)
{
    Address a;
    std::string error;
    RO_CHECK(parseAddress("192.168.1.5:9000", a, error));
    RO_CHECK(a.transport == Transport::Tcp);
    RO_CHECK_EQ(a.url, std::string("tcp://192.168.1.5:9000"));
}

RO_TEST(address_listen_url_defaults_to_loopback_in_normalization)
{
    Address a;
    std::string error;
    RO_CHECK(parseAddress("ws://:4343", a, error));
    RO_CHECK(a.host.empty());  // empty host means "any interface" when listening
    RO_CHECK_EQ(a.url, std::string("ws://127.0.0.1:4343"));
}

RO_TEST(address_rejects_unsupported_schemes)
{
    Address a;
    std::string error;
    RO_CHECK(!parseAddress("wss://secure:443", a, error));
    RO_CHECK(!error.empty());
    RO_CHECK(error.find("wss://") != std::string::npos);

    error.clear();
    RO_CHECK(!parseAddress("http://example.com:80", a, error));
    RO_CHECK(error.find("tcp://") != std::string::npos);
}

RO_TEST(address_rejects_bad_ports)
{
    Address a;
    std::string error;
    RO_CHECK(!parseAddress("ws://host", a, error));   // missing port
    error.clear();
    RO_CHECK(!parseAddress("tcp://host:0", a, error));  // port 0
    error.clear();
    RO_CHECK(!parseAddress("tcp://host:70000", a, error));  // out of range
}
