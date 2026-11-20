#include "net.h"

#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
  #if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
    #undef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601   //needed for inet_pton on MinGW
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
  using native_t = SOCKET;
  static int  lastErr()               { return WSAGetLastError(); }
  static bool wouldBlock(int e)       { return e == WSAEWOULDBLOCK; }
  static bool connectPending(int e)   { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
  static void closeNative(native_t s) { closesocket(s); }
  static const int E_REFUSED = WSAECONNREFUSED, E_INUSE = WSAEADDRINUSE, E_UNREACH = WSAENETUNREACH,
                   E_HOSTUNREACH = WSAEHOSTUNREACH, E_TIMEDOUT = WSAETIMEDOUT;
#else
  #include <arpa/inet.h>
  #include <cerrno>
  #include <fcntl.h>
  #include <ifaddrs.h>
  #include <net/if.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <signal.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using native_t = int;
  static int  lastErr()               { return errno; }
  static bool wouldBlock(int e)       { return e == EAGAIN || e == EWOULDBLOCK; }
  static bool connectPending(int e)   { return e == EINPROGRESS; }
  static void closeNative(native_t s) { close(s); }
  static const int E_REFUSED = ECONNREFUSED, E_INUSE = EADDRINUSE, E_UNREACH = ENETUNREACH,
                   E_HOSTUNREACH = EHOSTUNREACH, E_TIMEDOUT = ETIMEDOUT;
#endif

static native_t N(sock_t s)   { return (native_t)s; }
static sock_t   H(native_t s) { return (sock_t)s; }

static double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static std::string errText(int e) {
    if (e == E_REFUSED) return "connection refused (is the host running and the port right?)";
    if (e == E_INUSE) return "port already in use";
    if (e == E_UNREACH || e == E_HOSTUNREACH) return "network unreachable (are both on the same network?)";
    if (e == E_TIMEDOUT) return "timed out";
    return "socket error " + std::to_string(e);
}

static bool setNonBlocking(native_t s) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
#else
    int f = fcntl(s, F_GETFL, 0);
    return f != -1 && fcntl(s, F_SETFL, f | O_NONBLOCK) == 0;
#endif
}

//IPv4 or "localhost" only, DNS could block
static bool makeAddr(const std::string& ip, uint16_t port, sockaddr_in& a) {
    a = sockaddr_in{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    std::string host = (ip == "localhost") ? "127.0.0.1" : ip;
    return inet_pton(AF_INET, host.c_str(), &a.sin_addr) == 1;
}

static native_t openListener(uint16_t port, int& err, int backlog = 1) {
    native_t ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (H(ls) == BAD_SOCK) { err = lastErr(); return ls; }
#ifndef _WIN32
    int yes = 1;   //on Windows this would allow port stealing
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, backlog) != 0) {
        err = lastErr();
        closeNative(ls);
        return (native_t)BAD_SOCK;
    }
    return ls;
}

bool netInit() {
#ifdef _WIN32
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
#else
    signal(SIGPIPE, SIG_IGN);   //send to a closed peer returns an error instead of killing us
    return true;
#endif
}

void netShutdown() {
#ifdef _WIN32
    WSACleanup();
#endif
}


sock_t hostAndWait(uint16_t port) {
    int err = 0;
    native_t ls = openListener(port, err);
    if (H(ls) == BAD_SOCK) return BAD_SOCK;
    native_t s = accept(ls, nullptr, nullptr);
    closeNative(ls);
    return H(s);
}

sock_t joinPeer(const std::string& ip, uint16_t port) {
    sockaddr_in a;
    if (!makeAddr(ip, port, a)) return BAD_SOCK;
    native_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (H(s) == BAD_SOCK) return BAD_SOCK;
    if (connect(s, (sockaddr*)&a, sizeof a) != 0) {
        closeNative(s);
        return BAD_SOCK;
    }
    return H(s);
}


ConnState Connector::fail(const std::string& why) {
    cancel();
    err_ = why;
    state_ = ConnState::Failed;
    return state_;
}

bool Connector::hostStart(uint16_t port) {
    cancel();
    hosting_ = true;
    int err = 0;
    native_t ls = openListener(port, err);
    if (H(ls) == BAD_SOCK) { fail("cannot listen on port " + std::to_string(port) + ": " + errText(err)); return false; }
    if (!setNonBlocking(ls)) { closeNative(ls); fail("cannot make the listener non-blocking"); return false; }
    listen_ = H(ls);
    state_ = ConnState::Waiting;
    return true;
}

bool Connector::joinStart(const std::string& ip, uint16_t port, double timeoutSec) {
    cancel();
    hosting_ = false;
    sockaddr_in a;
    if (!makeAddr(ip, port, a)) { fail("\"" + ip + "\" is not an IPv4 address like 192.168.1.23"); return false; }
    native_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (H(s) == BAD_SOCK) { fail(errText(lastErr())); return false; }
    if (!setNonBlocking(s)) { closeNative(s); fail("cannot make the socket non-blocking"); return false; }
    sock_ = H(s);
    if (connect(s, (sockaddr*)&a, sizeof a) == 0) {
        state_ = ConnState::Connected;
        return true;
    }
    int e = lastErr();
    if (!connectPending(e)) { fail(errText(e)); return false; }
    deadline_ = nowSec() + timeoutSec;
    state_ = ConnState::Waiting;
    return true;
}

ConnState Connector::poll() {
    if (state_ != ConnState::Waiting) return state_;

    if (hosting_) {
        native_t s = accept(N(listen_), nullptr, nullptr);
        if (H(s) == BAD_SOCK) {
            int e = lastErr();
            return wouldBlock(e) ? state_ : fail("accept failed: " + errText(e));
        }
        closeNative(N(listen_));
        listen_ = BAD_SOCK;
        sock_ = H(s);
        state_ = ConnState::Connected;
        return state_;
    }

    //Windows reports a failed connect in the except set, POSIX in SO_ERROR
    fd_set wr, ex;
    FD_ZERO(&wr);
    FD_ZERO(&ex);
    FD_SET(N(sock_), &wr);
    FD_SET(N(sock_), &ex);
    timeval tv{0, 0};
    int n = select((int)N(sock_) + 1, nullptr, &wr, &ex, &tv);
    if (n < 0) return fail("select failed: " + errText(lastErr()));
    if (n == 0) {
        if (nowSec() > deadline_) return fail("no answer from the host (wrong IP, firewall, or different network)");
        return state_;
    }
    int soErr = 0;
    socklen_t len = sizeof soErr;
    getsockopt(N(sock_), SOL_SOCKET, SO_ERROR, (char*)&soErr, &len);
    if (FD_ISSET(N(sock_), &ex) || soErr != 0) return fail(errText(soErr ? soErr : E_REFUSED));
    state_ = ConnState::Connected;
    return state_;
}

sock_t Connector::take() {
    if (state_ != ConnState::Connected) return BAD_SOCK;
    sock_t s = sock_;
    sock_ = BAD_SOCK;
    state_ = ConnState::Idle;
    return s;
}

void Connector::cancel() {
    if (listen_ != BAD_SOCK) closeNative(N(listen_));
    if (sock_ != BAD_SOCK) closeNative(N(sock_));
    listen_ = sock_ = BAD_SOCK;
    state_ = ConnState::Idle;
    err_.clear();
}

sock_t listenOn(uint16_t port, std::string& errOut) {
    int err = 0;
    native_t ls = openListener(port, err, 4);
    if (H(ls) == BAD_SOCK) { errOut = "cannot listen on port " + std::to_string(port) + ": " + errText(err); return BAD_SOCK; }
    if (!setNonBlocking(ls)) { closeNative(ls); errOut = "cannot make the listener non-blocking"; return BAD_SOCK; }
    return H(ls);
}

sock_t acceptClient(sock_t listener) {
    return H(accept(N(listener), nullptr, nullptr));
}

void waitReadable(const std::vector<sock_t>& socks, int ms) {
    fd_set rd;
    FD_ZERO(&rd);
    native_t top = 0;
    int count = 0;
    for (sock_t s : socks) {
        if (s == BAD_SOCK) continue;
        FD_SET(N(s), &rd);
        if (N(s) > top) top = N(s);
        ++count;
    }
    if (count == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); return; }   //Windows select fails on empty sets
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    select((int)top + 1, &rd, nullptr, nullptr, &tv);
}

std::vector<std::string> localAddresses() {
    std::vector<std::string> out;
    auto add = [&](in_addr ia) {
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &ia, ip, sizeof ip);
        if (std::strncmp(ip, "127.", 4) != 0) out.push_back(ip);
    };
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf;
    ULONG rc;
    do {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                  nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &size);
    } while (rc == ERROR_BUFFER_OVERFLOW);
    if (rc != NO_ERROR) return out;
    for (auto* ad = (IP_ADAPTER_ADDRESSES*)buf.data(); ad; ad = ad->Next) {
        if (ad->OperStatus != IfOperStatusUp) continue;
        for (auto* u = ad->FirstUnicastAddress; u; u = u->Next)
            if (u->Address.lpSockaddr->sa_family == AF_INET) add(((sockaddr_in*)u->Address.lpSockaddr)->sin_addr);
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (ifaddrs* i = list; i; i = i->ifa_next)
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && (i->ifa_flags & IFF_UP))
            add(((sockaddr_in*)i->ifa_addr)->sin_addr);
    freeifaddrs(list);
#endif
    return out;
}


bool prepareSocket(sock_t s) {
    int one = 1;
    setsockopt(N(s), IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
    return setNonBlocking(N(s));
}

bool sendLine(sock_t s, const std::string& line) {
    std::string msg = line + "\n";
    size_t off = 0;
    while (off < msg.size()) {
        int n = (int)send(N(s), msg.data() + off, (int)(msg.size() - off), 0);
        if (n > 0) { off += n; continue; }
        if (n < 0 && wouldBlock(lastErr())) continue;
        return false;
    }
    return true;
}

bool LineReader::pump(sock_t s) {
    char tmp[512];
    for (;;) {
        int n = (int)recv(N(s), tmp, sizeof tmp, 0);
        if (n > 0) {
            buf.append(tmp, n);
            if (buf.size() > MAX_BUF) return false;
            continue;
        }
        if (n == 0) return false;
        return wouldBlock(lastErr());
    }
}

bool LineReader::next(std::string& out) {
    size_t p = buf.find('\n');
    if (p == std::string::npos) return false;
    out = buf.substr(0, p);
    buf.erase(0, p + 1);
    if (!out.empty() && out.back() == '\r') out.pop_back();
    return true;
}

std::string peerAddress(sock_t s) {
    sockaddr_in a{};
    socklen_t len = sizeof a;
    if (getpeername(N(s), (sockaddr*)&a, &len) != 0) return "unknown";
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
    return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}

void closeSock(sock_t s) {
    if (s != BAD_SOCK) closeNative(N(s));
}
