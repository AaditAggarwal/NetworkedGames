#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "../common/net.h"
#include "../server/game_server.h"


static int g_failures = 0;
static std::vector<std::pair<const char*, std::function<void()>>>& registry() {
    static std::vector<std::pair<const char*, std::function<void()>>> r;
    return r;
}
struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, fn}); }
};
#define TEST(name)                                         \
    static void name();                                    \
    static Registrar reg_##name(#name, name);              \
    static void name()
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)


static const uint16_t PORT = 5099;

static bool waitBoth(Connector& a, Connector& b) {
    for (int i = 0; i < 500; ++i) {
        a.poll();
        b.poll();
        if (a.state() == ConnState::Connected && b.state() == ConnState::Connected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static std::vector<std::string> readLines(LineReader& r, sock_t s, size_t want) {
    std::vector<std::string> got;
    std::string line;
    for (int i = 0; i < 200 && got.size() < want; ++i) {
        r.pump(s);
        while (r.next(line)) got.push_back(line);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return got;
}

TEST(connector_host_and_join_without_blocking) {
    Connector h, j;
    CHECK(h.hostStart(PORT));
    CHECK(h.poll() == ConnState::Waiting);
    CHECK(j.joinStart("127.0.0.1", PORT));
    CHECK(waitBoth(h, j));
    sock_t a = h.take(), b = j.take();
    CHECK(a != BAD_SOCK && b != BAD_SOCK);
    CHECK(prepareSocket(a) && prepareSocket(b));
    CHECK(peerAddress(a).rfind("127.0.0.1:", 0) == 0);

    CHECK(sendLine(b, "HELLO Bob\nREADY"));
    CHECK(sendLine(b, "FIRE B7\r"));
    LineReader r;
    std::vector<std::string> got = readLines(r, a, 3);
    CHECK(got.size() == 3);
    if (got.size() == 3) CHECK(got[0] == "HELLO Bob" && got[1] == "READY" && got[2] == "FIRE B7");

    closeSock(b);
    bool closed = false;
    for (int i = 0; i < 200 && !closed; ++i) {
        closed = !r.pump(a);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(closed);
    closeSock(a);
}

TEST(blocking_host_and_join) {
    sock_t hs = BAD_SOCK;
    std::thread t([&] { hs = hostAndWait(PORT + 1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sock_t js = joinPeer("127.0.0.1", PORT + 1);
    t.join();
    CHECK(hs != BAD_SOCK && js != BAD_SOCK);
    prepareSocket(hs);
    CHECK(sendLine(js, "BYE"));
    LineReader r;
    std::vector<std::string> got = readLines(r, hs, 1);
    CHECK(got.size() == 1 && got[0] == "BYE");
    closeSock(hs);
    closeSock(js);
}

TEST(join_errors) {
    Connector j;
    CHECK(!j.joinStart("not-an-ip", PORT));
    CHECK(j.state() == ConnState::Failed);

    CHECK(j.joinStart("127.0.0.1", PORT + 2));
    for (int i = 0; i < 700 && j.poll() == ConnState::Waiting; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(j.state() == ConnState::Failed);
    std::printf("  closed port: %s\n", j.error().c_str());

    Connector k;
    if (k.joinStart("10.255.255.1", PORT, 1.0)) {
        for (int i = 0; i < 300 && k.poll() == ConnState::Waiting; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(k.state() == ConnState::Failed);
    std::printf("  unroutable: %s\n", k.error().c_str());
}

TEST(host_port_in_use) {
    Connector a, b;
    CHECK(a.hostStart(PORT + 3));
    CHECK(!b.hostStart(PORT + 3));
    std::printf("  second host: %s\n", b.error().c_str());
}

TEST(local_addresses_listed) {
    std::vector<std::string> ips = localAddresses();
    std::printf("  local:");
    for (auto& ip : ips) std::printf(" %s", ip.c_str());
    std::printf("\n");
    for (auto& ip : ips) CHECK(ip.rfind("127.", 0) != 0);
}

struct Client {
    sock_t s = BAD_SOCK;
    LineReader r;
    std::vector<std::string> got;
};

//step the server until c has `want` lines or about 2s pass
static bool serve(GameServer& srv, Client& c, size_t want) {
    std::string line;
    for (int i = 0; i < 400 && c.got.size() < want; ++i) {
        srv.step(5);
        c.r.pump(c.s);
        while (c.r.next(line)) c.got.push_back(line);
    }
    return c.got.size() >= want;
}

static Client joinServer(GameServer& srv, uint16_t port) {
    Client c;
    Connector j;
    j.joinStart("127.0.0.1", port);
    for (int i = 0; i < 400 && j.poll() == ConnState::Waiting; ++i) srv.step(5);
    c.s = j.take();
    if (c.s != BAD_SOCK) prepareSocket(c.s);
    return c;
}

TEST(game_server_over_loopback) {
    const uint16_t port = PORT + 4;
    GameServer srv;
    CHECK(srv.start(port));
    GameServer second;
    CHECK(!second.start(port));

    Client a = joinServer(srv, port), b = joinServer(srv, port);
    CHECK(a.s != BAD_SOCK && b.s != BAD_SOCK);
    sendLine(a.s, "HELLO Ann");
    CHECK(serve(srv, a, 2));
    sendLine(b.s, "HELLO Bob");
    CHECK(serve(srv, b, 1));
    CHECK(a.got[0] == "WELCOME 1" && b.got[0] == "WELCOME 2");

    Client c = joinServer(srv, port);
    CHECK(serve(srv, c, 1) && c.got[0] == "ERR server full");
    closeSock(c.s);

    const char* fleet[5] = {"CARRIER A1 H", "BATTLESHIP B1 H", "CRUISER C1 H", "SUBMARINE D1 H", "DESTROYER E1 H"};
    for (const char* f : fleet) {
        sendLine(a.s, std::string("PLACE ") + f);
        sendLine(b.s, std::string("PLACE ") + f);
    }
    for (int i = 0; i < 20 && (a.got.back() != "YOUR_TURN" || b.got.back() != "OPP_TURN"); ++i) {
        serve(srv, a, a.got.size() + 1);
        serve(srv, b, b.got.size() + 1);
    }
    CHECK(a.got.back() == "YOUR_TURN" && b.got.back() == "OPP_TURN");

    sendLine(a.s, "FIRE A1");
    size_t na = a.got.size(), nb = b.got.size();
    CHECK(serve(srv, a, na + 2) && a.got[na] == "RESULT A1 HIT" && a.got[na + 1] == "OPP_TURN");
    CHECK(serve(srv, b, nb + 2) && b.got[nb] == "OPP_FIRED A1 HIT" && b.got[nb + 1] == "YOUR_TURN");

    closeSock(b.s);
    CHECK(serve(srv, a, a.got.size() + 1));
    bool left = false;
    for (auto& l : a.got) left = left || l.rfind("OPP_LEFT", 0) == 0;
    CHECK(left);
    closeSock(a.s);
    srv.stop();
    CHECK(!srv.running());
}

int main() {
    if (!netInit()) { std::printf("netInit failed\n"); return 1; }
    for (auto& t : registry()) {
        std::printf("%s\n", t.first);
        t.second();
    }
    netShutdown();
    std::printf(g_failures ? "%d check(s) failed\n" : "all tests passed\n", g_failures);
    return g_failures ? 1 : 0;
}
