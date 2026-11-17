#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "../src/demo_fleet.h"
#include "../src/net.h"
#include "../src/peer_game.h"

using namespace p2p;

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

static void exchange(PeerGame& a, PeerGame& b) {
    for (bool moved = true; moved;) {
        moved = false;
        for (auto& l : a.takeOutgoing()) { b.onLine(l); moved = true; }
        for (auto& l : b.takeOutgoing()) { a.onLine(l); moved = true; }
    }
}

static bool has(const std::vector<std::string>& v, const std::string& s) {
    for (auto& x : v)
        if (x == s) return true;
    return false;
}

struct Pair {
    DemoFleet hostFleet, joinFleet;
    PeerGame host{true, "Alice", hostFleet};
    PeerGame join{false, "Bob", joinFleet};
    void connect() { host.onConnected(); join.onConnected(); exchange(host, join); }
    void start() { connect(); host.onAllPlaced(); join.onAllPlaced(); exchange(host, join); }
};


TEST(cells_parse_and_name) {
    int r, c;
    CHECK(parseCell("A1", r, c) && r == 0 && c == 0);
    CHECK(parseCell("j10", r, c) && r == 9 && c == 9);
    CHECK(!parseCell("K1", r, c));
    CHECK(!parseCell("A11", r, c));
    CHECK(!parseCell("A0", r, c));
    CHECK(!parseCell("B", r, c));
    CHECK(cellName(1, 6) == "B7");
}


TEST(hello_both_ways) {
    Pair p;
    p.host.onConnected();
    p.join.onConnected();
    CHECK(has(p.host.takeOutgoing(), "HELLO Alice"));
    CHECK(has(p.join.takeOutgoing(), "HELLO Bob"));
    p.host.onLine("HELLO Bob");
    CHECK(p.host.peerName() == "Bob");
    CHECK(p.host.state() == State::Placing);
}

TEST(host_fires_first) {
    Pair p;
    p.start();
    CHECK(p.host.state() == State::MyTurn);
    CHECK(p.join.state() == State::PeerTurn);
}

TEST(early_ready_is_kept) {
    Pair p;
    p.connect();
    p.join.onAllPlaced();
    exchange(p.host, p.join);
    CHECK(p.host.state() == State::Placing);
    CHECK(p.host.peerReady());
    p.host.onAllPlaced();
    exchange(p.host, p.join);
    CHECK(p.host.state() == State::MyTurn);
    CHECK(p.join.state() == State::PeerTurn);
}

TEST(full_game_to_defeat) {
    Pair p;
    p.start();
    const int lens[5] = {5, 4, 3, 3, 2};
    int miss = 0;
    for (int ship = 0; ship < 5; ++ship)
        for (int k = 0; k < lens[ship]; ++k) {
            CHECK(p.host.fire(ship * 2, k));
            exchange(p.host, p.join);
            if (p.host.state() == State::GameOver) break;
            CHECK(p.join.fire(1 + 2 * (miss / 10), miss % 10));
            ++miss;
            exchange(p.host, p.join);
        }
    CHECK(p.host.state() == State::GameOver && p.host.won());
    CHECK(p.join.state() == State::GameOver && !p.join.won());
    CHECK(p.host.enemySunk().size() == 5);
    CHECK(p.host.shotsFired() == 17 && p.host.shotsHit() == 17);
    CHECK(p.host.enemy(0, 0) == Mark::Hit);
    CHECK(p.join.onMe(8, 1) == Mark::Sunk);
}

TEST(sunk_names_the_ship) {
    Pair p;
    p.start();
    p.host.fire(8, 0);
    exchange(p.host, p.join);
    p.join.fire(1, 0);
    exchange(p.host, p.join);
    p.host.fire(8, 1);
    std::vector<std::string> fire = p.host.takeOutgoing();
    CHECK(has(fire, "FIRE I2"));
    p.join.onLine("FIRE I2");
    CHECK(has(p.join.takeOutgoing(), "RESULT I2 SUNK DESTROYER"));
}

TEST(fire_out_of_turn_gets_err) {
    Pair p;
    p.start();
    p.host.onLine("FIRE A1");
    CHECK(has(p.host.takeOutgoing(), "ERR not your turn"));
    CHECK(p.host.state() == State::MyTurn);
    CHECK(!p.join.fire(0, 0));
    CHECK(p.join.takeOutgoing().empty());
}

TEST(malformed_lines_get_err) {
    Pair p;
    p.start();
    p.join.onLine("FIRE Z99");
    CHECK(has(p.join.takeOutgoing(), "ERR bad cell"));
    p.join.onLine("FIRE");
    CHECK(has(p.join.takeOutgoing(), "ERR bad cell"));
    p.join.onLine("DANCE");
    CHECK(has(p.join.takeOutgoing(), "ERR unknown message DANCE"));
    CHECK(p.join.state() == State::PeerTurn);
}

TEST(err_is_never_answered) {
    Pair p;
    p.start();
    p.join.onLine("ERR something");
    CHECK(p.join.takeOutgoing().empty());
    CHECK(p.join.lastError() == "something");
}

TEST(repeat_fire_rejected_turn_kept) {
    Pair p;
    p.start();
    p.host.fire(1, 0);
    exchange(p.host, p.join);
    p.join.fire(1, 0);
    exchange(p.host, p.join);
    CHECK(!p.host.fire(1, 0));
    p.join.onLine("FIRE B1");
    CHECK(has(p.join.takeOutgoing(), "ERR already fired at that cell"));
    CHECK(p.join.state() == State::PeerTurn);
}

TEST(err_on_fire_gives_turn_back) {
    Pair p;
    p.start();
    p.host.fire(0, 0);
    p.host.takeOutgoing();
    CHECK(p.host.state() == State::WaitResult);
    p.host.onLine("ERR already fired at that cell");
    CHECK(p.host.state() == State::MyTurn);
}

TEST(result_must_match_fire) {
    Pair p;
    p.start();
    p.host.fire(0, 0);
    p.host.takeOutgoing();
    p.host.onLine("RESULT C3 HIT");
    CHECK(has(p.host.takeOutgoing(), "ERR RESULT does not match my FIRE"));
    CHECK(p.host.state() == State::WaitResult);
    p.join.onLine("RESULT A1 HIT");
    CHECK(has(p.join.takeOutgoing(), "ERR unexpected RESULT"));
}

TEST(disconnect_and_bye) {
    Pair p;
    p.start();
    p.host.onDisconnect();
    CHECK(p.host.state() == State::GameOver && p.host.peerLeft());
    p.join.quit();
    CHECK(has(p.join.takeOutgoing(), "BYE"));
    Pair q;
    q.start();
    q.host.onLine("BYE");
    CHECK(q.host.state() == State::GameOver && q.host.peerLeft());
}


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
