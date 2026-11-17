//don't include winsock2.h here, it clashes with raylib
#pragma once

#include <cstdint>
#include <string>
#include <vector>

using sock_t = std::intptr_t;
constexpr sock_t BAD_SOCK = -1;

bool netInit();
void netShutdown();

sock_t hostAndWait(uint16_t port);
sock_t joinPeer(const std::string& ip, uint16_t port);

enum class ConnState { Idle, Waiting, Connected, Failed };

class Connector {
public:
    Connector() = default;
    ~Connector() { cancel(); }
    Connector(const Connector&) = delete;
    Connector& operator=(const Connector&) = delete;

    bool hostStart(uint16_t port);
    bool joinStart(const std::string& ip, uint16_t port, double timeoutSec = 5.0);
    ConnState poll();
    sock_t take();
    void cancel();

    ConnState state() const { return state_; }
    const std::string& error() const { return err_; }
    bool hosting() const { return hosting_; }

private:
    ConnState fail(const std::string& why);
    sock_t listen_ = BAD_SOCK;
    sock_t sock_ = BAD_SOCK;
    ConnState state_ = ConnState::Idle;
    bool hosting_ = false;
    double deadline_ = 0;
    std::string err_;
};

std::vector<std::string> localAddresses();

bool sendLine(sock_t s, const std::string& line);
bool prepareSocket(sock_t s);
std::string peerAddress(sock_t s);
void closeSock(sock_t s);

class LineReader {
public:
    bool pump(sock_t s);
    bool next(std::string& out);
    static constexpr size_t MAX_BUF = 64 * 1024;
private:
    std::string buf;
};
