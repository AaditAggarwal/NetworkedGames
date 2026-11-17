// conn.h - the network interface the client GUI uses.
//
// The GUI (role 4) only calls these functions. The implementation (socket
// creation, non-blocking connect, send/recv, splitting the byte stream on '\n')
// belongs to Networking (role 1), e.g. as a thin wrapper over common/net.h's
// socket helpers and LineReader. The GUI calls poll() once per frame, so no
// function here may block.
#pragma once

#include <string>

class Conn {
public:
    enum class Status { Idle, Connecting, Connected, Failed, Closed };

    Conn();
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    // Start a non-blocking connect. Returns false (and sets error()) if it
    // failed right away, e.g. the host name could not be resolved.
    bool start(const std::string& host, int port);

    // Advance the connect, flush pending writes, read whatever has arrived.
    Status poll();

    // Pop one complete server line (without the trailing \n or \r\n).
    bool nextLine(std::string& out);

    // Queue one message; "\n" is appended. Returns false if not connected.
    bool send(const std::string& line);

    // Close the socket (sends FIN). Status becomes Closed.
    void close();

    Status status() const;
    const std::string& error() const;   // reason after Failed / Closed
    const std::string& peer() const;    // "ip:port" of the server, for the status strip

private:
    struct Impl;   // defined by the networking implementation
    Impl* impl_;
};
