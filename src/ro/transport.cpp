#include "transport.h"

#include <asio2/asio2.hpp>

#include <map>
#include <mutex>
#include <string>

namespace ro {

namespace {

bool isWebsocketUrl(const std::string &url)
{
    return url.rfind("ws://", 0) == 0;
}

// ---------------------------------------------------------------------------
// TCP
// ---------------------------------------------------------------------------

class TcpConnection : public Connection {
public:
    explicit TcpConnection(std::shared_ptr<asio2::tcp_session> session)
        : session_(std::move(session))
    {
    }

    void send(std::string frame) override
    {
        session_->async_send(std::move(frame));
    }
    void close() override { session_->stop(); }
    std::string remote() const override { return session_->remote_address(); }

private:
    std::shared_ptr<asio2::tcp_session> session_;
};

class TcpClientTransport : public ClientTransport {
public:
    bool start(std::string host, uint16_t port) override
    {
        if (!client_.async_start(std::move(host), port))
            return false;
        started_ = true;
        return true;
    }
    void stop() override { client_.stop(); }
    void send(std::string frame) override
    {
        if (started_)
            client_.async_send(std::move(frame));
    }
    void onConnected(ConnectedFn fn) override { client_.bind_connect(std::move(fn)); }
    void onDisconnected(DisconnectedFn fn) override { client_.bind_disconnect(std::move(fn)); }
    void onData(DataFn fn) override { client_.bind_recv(std::move(fn)); }
    void setAutoReconnect(bool enable, uint32_t delaySeconds) override
    {
        client_.set_auto_reconnect(enable, std::chrono::seconds(delaySeconds));
    }

private:
    asio2::tcp_client client_;
    bool started_ = false;
};

class TcpServerTransport : public ServerTransport {
public:
    bool listen(std::string host, uint16_t port) override
    {
        if (!server_.start(std::move(host), port))
            return false;
        listening_ = true;
        return true;
    }
    void stop() override
    {
        if (listening_) {
            server_.stop();
            listening_ = false;
        }
    }

    void onAccepted(AcceptedFn fn) override
    {
        server_.bind_accept([this, fn](std::shared_ptr<asio2::tcp_session> &session) {
            auto it = connections_.find(session.get());
            auto conn = it == connections_.end() ? std::make_shared<TcpConnection>(session)
                                                  : it->second;
            connections_[session.get()] = conn;
            fn(conn);
        });
    }
    void onDisconnected(DisconnectedFn fn) override
    {
        server_.bind_disconnect([this, fn](std::shared_ptr<asio2::tcp_session> &session) {
            if (auto conn = lookup(session.get()))
                fn(conn.get());
            connections_.erase(session.get());
        });
    }
    void onData(DataFn fn) override
    {
        server_.bind_recv([this, fn](std::shared_ptr<asio2::tcp_session> &session,
                                     std::string_view data) {
            if (auto conn = lookup(session.get()))
                fn(conn, data);
        });
    }

private:
    std::shared_ptr<Connection> lookup(asio2::tcp_session *session)
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        auto it = connections_.find(session);
        return it == connections_.end() ? nullptr : it->second;
    }

    asio2::tcp_server server_;
    bool listening_ = false;
    std::mutex mapMutex_;
    std::map<asio2::tcp_session *, std::shared_ptr<Connection>> connections_;
};

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

class WsConnection : public Connection {
public:
    explicit WsConnection(std::shared_ptr<asio2::ws_session> session)
        : session_(std::move(session))
    {
    }

    void send(std::string frame) override
    {
        session_->async_send(std::move(frame));
    }
    void close() override { session_->stop(); }
    std::string remote() const override { return session_->remote_address(); }

private:
    std::shared_ptr<asio2::ws_session> session_;
};

class WsClientTransport : public ClientTransport {
public:
    bool start(std::string host, uint16_t port) override
    {
        if (!client_.async_start(std::move(host), port))
            return false;
        started_ = true;
        return true;
    }
    void stop() override { client_.stop(); }
    void send(std::string frame) override
    {
        if (started_)
            client_.async_send(std::move(frame));
    }
    void onConnected(ConnectedFn fn) override { client_.bind_connect(std::move(fn)); }
    void onDisconnected(DisconnectedFn fn) override { client_.bind_disconnect(std::move(fn)); }
    void onData(DataFn fn) override { client_.bind_recv(std::move(fn)); }
    void setAutoReconnect(bool enable, uint32_t delaySeconds) override
    {
        client_.set_auto_reconnect(enable, std::chrono::seconds(delaySeconds));
    }

private:
    asio2::ws_client client_;
    bool started_ = false;
};

class WsServerTransport : public ServerTransport {
public:
    bool listen(std::string host, uint16_t port) override
    {
        if (!server_.start(std::move(host), port))
            return false;
        listening_ = true;
        return true;
    }
    void stop() override
    {
        if (listening_) {
            server_.stop();
            listening_ = false;
        }
    }

    void onAccepted(AcceptedFn fn) override
    {
        server_.bind_accept([this, fn](std::shared_ptr<asio2::ws_session> &session) {
            auto it = connections_.find(session.get());
            auto conn = it == connections_.end() ? std::make_shared<WsConnection>(session)
                                                  : it->second;
            connections_[session.get()] = conn;
            fn(conn);
        });
    }
    void onDisconnected(DisconnectedFn fn) override
    {
        server_.bind_disconnect([this, fn](std::shared_ptr<asio2::ws_session> &session) {
            if (auto conn = lookup(session.get()))
                fn(conn.get());
            connections_.erase(session.get());
        });
    }
    void onData(DataFn fn) override
    {
        server_.bind_recv([this, fn](std::shared_ptr<asio2::ws_session> &session,
                                     std::string_view data) {
            if (auto conn = lookup(session.get()))
                fn(conn, data);
        });
    }

private:
    std::shared_ptr<Connection> lookup(asio2::ws_session *session)
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        auto it = connections_.find(session);
        return it == connections_.end() ? nullptr : it->second;
    }

    asio2::ws_server server_;
    bool listening_ = false;
    std::mutex mapMutex_;
    std::map<asio2::ws_session *, std::shared_ptr<Connection>> connections_;
};

}  // namespace

std::shared_ptr<ClientTransport> makeClient(const std::string &url)
{
    if (isWebsocketUrl(url))
        return std::make_shared<WsClientTransport>();
    return std::make_shared<TcpClientTransport>();
}

std::shared_ptr<ServerTransport> makeServer(const std::string &url)
{
    if (isWebsocketUrl(url))
        return std::make_shared<WsServerTransport>();
    return std::make_shared<TcpServerTransport>();
}

}  // namespace ro
