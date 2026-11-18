//Conn for the GUI, built on common/net.h
#include "conn.h"

#include "../common/net.h"

struct Conn::Impl {
    Connector c;
    sock_t s = BAD_SOCK;
    LineReader r;
    Status st = Status::Idle;
    std::string err, peer;
};

Conn::Conn() : impl_(new Impl) {
    static bool inited = netInit();
    (void)inited;
}

Conn::~Conn() { close(); delete impl_; }

bool Conn::start(const std::string& host, int port) {
    close();
    impl_->r = LineReader();
    impl_->err.clear();
    impl_->peer = host + ":" + std::to_string(port);
    if (!impl_->c.joinStart(host, (uint16_t)port)) {
        impl_->st = Status::Failed;
        impl_->err = impl_->c.error();
        return false;
    }
    impl_->st = Status::Connecting;
    return true;
}

Conn::Status Conn::poll() {
    Impl& m = *impl_;
    if (m.st == Status::Connecting) {
        ConnState cs = m.c.poll();
        if (cs == ConnState::Failed) {
            m.st = Status::Failed;
            m.err = m.c.error();
        } else if (cs == ConnState::Connected) {
            m.s = m.c.take();
            prepareSocket(m.s);
            m.peer = peerAddress(m.s);
            m.st = Status::Connected;
        }
    }
    if (m.st == Status::Connected && !m.r.pump(m.s)) {
        m.st = Status::Closed;
        m.err = "server closed the connection";
    }
    return m.st;
}

bool Conn::nextLine(std::string& out) { return impl_->r.next(out); }

bool Conn::send(const std::string& line) {
    return impl_->st == Status::Connected && sendLine(impl_->s, line);
}

void Conn::close() {
    impl_->c.cancel();
    closeSock(impl_->s);
    impl_->s = BAD_SOCK;
    if (impl_->st != Status::Idle) impl_->st = Status::Closed;
}

Conn::Status Conn::status() const { return impl_->st; }
const std::string& Conn::error() const { return impl_->err; }
const std::string& Conn::peer() const { return impl_->peer; }
