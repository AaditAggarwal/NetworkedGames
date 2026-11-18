//unit tests for server/game (role 2), same no-framework style as test_board.cpp
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../common/rules.h"
#include "../server/game.h"

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

using Lines = std::vector<std::string>;

//wraps a Game and collects what it sends to each player, like the server loop would
struct Harness {
    Game g;
    Lines out[2];
    std::vector<int> kicks;
    Lines log;

    explicit Harness(double timeout = 0) : g(timeout) {}

    void pump() {
        for (OutMsg& m : g.takeOutgoing()) out[m.player].push_back(m.text);
        for (int k : g.takeKicks()) kicks.push_back(k);
        for (std::string& l : g.takeLog()) log.push_back(l);
    }
    void connect(int p) { g.onConnect(p, "10.0.0." + std::to_string(p + 1) + ":5555"); pump(); }
    void say(int p, const std::string& line) { g.onLine(p, line); pump(); }
    void leave(int p) { g.onDisconnect(p); pump(); }
    void tick(double t) { g.tick(t); pump(); }

    //everything sent to p since the last take(p)
    Lines take(int p) { Lines v; v.swap(out[p]); return v; }
    std::string last(int p) const { return out[p].empty() ? "" : out[p].back(); }
};

static bool startsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

//same fleet for both players: every ship horizontal from column 1 on rows A, C, E, G, I
static const char* FLEET[NUM_SHIPS] = {
    "PLACE CARRIER A1 H", "PLACE BATTLESHIP C1 H", "PLACE CRUISER E1 H", "PLACE SUBMARINE G1 H", "PLACE DESTROYER I1 H",
};
//the 17 cells that fleet covers, in order, so the last one sinks the DESTROYER
static Lines fleetCells() {
    Lines v;
    const char rows[NUM_SHIPS] = {'A', 'C', 'E', 'G', 'I'};
    for (int s = 0; s < NUM_SHIPS; ++s)
        for (int c = 1; c <= SHIPS[s].len; ++c) v.push_back(std::string(1, rows[s]) + std::to_string(c));
    return v;
}
//water cells for that fleet (rows B, D, F, H, J)
static std::string waterCell(int i) {
    return std::string(1, "BDFHJ"[i / 10]) + std::to_string(i % 10 + 1);
}

static void joinBoth(Harness& h) {
    h.connect(0);
    h.say(0, "HELLO Ann");
    h.connect(1);
    h.say(1, "HELLO Bob");
}
static void placeFleet(Harness& h, int p) {
    for (const char* line : FLEET) h.say(p, line);
}
//both joined and placed, P1 to move, output queues cleared
static void startGame(Harness& h) {
    joinBoth(h);
    placeFleet(h, 0);
    placeFleet(h, 1);
    h.take(0);
    h.take(1);
}

//---------------------------------------------------------------- HELLO

TEST(hello_gets_welcome_and_first_player_waits) {
    Harness h;
    h.connect(0);
    CHECK(h.out[0].empty());   //nothing until HELLO
    h.say(0, "HELLO Ann");
    CHECK((h.take(0) == Lines{"WELCOME 1", "WAIT"}));
    CHECK(h.g.phase() == Phase::WaitingForPlayers);

    h.connect(1);
    h.say(1, "HELLO Bob");
    CHECK((h.take(1) == Lines{"WELCOME 2"}));
    CHECK(h.take(0).empty());
    CHECK(h.g.phase() == Phase::Placing);
    CHECK(h.g.name(0) == "Ann" && h.g.name(1) == "Bob");
}

TEST(commands_before_hello_rejected) {
    Harness h;
    h.connect(0);
    h.say(0, "PLACE CARRIER A1 H");
    h.say(0, "FIRE A1");
    Lines v = h.take(0);
    CHECK(v.size() == 2 && startsWith(v[0], "ERR ") && startsWith(v[1], "ERR "));
    CHECK(!h.g.board(0).isPlaced(0));
}

TEST(hello_twice_and_without_name_rejected) {
    Harness h;
    h.connect(0);
    h.say(0, "HELLO");
    CHECK(startsWith(h.last(0), "ERR "));
    CHECK(!h.g.joined(0));
    h.say(0, "HELLO Ann");
    h.say(0, "HELLO Again");
    CHECK(h.last(0) == "ERR already said HELLO");
    CHECK(h.g.name(0) == "Ann");
}

TEST(name_is_one_token_and_capped) {
    Harness h;
    h.connect(0);
    h.say(0, "HELLO Bob Smith");
    CHECK(h.g.name(0) == "Bob_Smith");
    h.connect(1);
    h.say(1, "HELLO abcdefghijklmnopqrstuvwxyz");
    CHECK(h.g.name(1).size() == Game::MAX_NAME);
}

TEST(lenient_parsing_for_nc) {
    Harness h;
    h.connect(0);
    h.say(0, "");          //blank lines are ignored
    h.say(0, "   ");
    CHECK(h.out[0].empty());
    h.say(0, "hello Ann\r");
    CHECK(h.g.name(0) == "Ann");   //\r stripped
    h.say(0, "place carrier a1 h");
    CHECK(h.last(0) == "PLACE_OK CARRIER");   //reply is always canonical uppercase
}

TEST(unknown_command_rejected) {
    Harness h;
    h.connect(0);
    h.say(0, "HELLO Ann");
    h.take(0);
    h.say(0, "DANCE");
    CHECK(h.last(0) == "ERR unknown command DANCE");
}

//---------------------------------------------------------------- PLACE

TEST(first_player_can_place_while_waiting) {
    Harness h;
    h.connect(0);
    h.say(0, "HELLO Ann");
    h.say(0, "PLACE CARRIER A1 H");
    CHECK(h.last(0) == "PLACE_OK CARRIER");
    CHECK(h.g.board(0).isPlaced(0));
}

TEST(place_errors_leave_board_unchanged) {
    Harness h;
    joinBoth(h);
    h.say(0, "PLACE CARRIER A1 H");
    h.take(0);
    const char* bad[] = {
        "PLACE",                        //arg count
        "PLACE CARRIER A1",             //arg count
        "PLACE CARRIER A1 H extra",     //arg count
        "PLACE CANOE A1 H",             //unknown ship
        "PLACE CRUISER K1 H",           //bad cell
        "PLACE CRUISER A0 H",           //bad cell
        "PLACE CRUISER B1 X",           //bad orientation
        "PLACE CRUISER A9 H",           //off the board
        "PLACE CRUISER A3 V",           //overlaps the carrier
        "PLACE CARRIER J1 H",           //already placed
    };
    for (const char* line : bad) {
        h.say(0, line);
        CHECK(startsWith(h.last(0), "ERR "));
    }
    CHECK(h.take(0).size() == sizeof bad / sizeof bad[0]);
    CHECK(!h.g.board(0).isPlaced(2));
    CHECK(h.g.board(0).shipAt(0, 0) == 0);
}

TEST(opponent_never_sees_placements) {
    Harness h;
    joinBoth(h);
    h.take(1);
    placeFleet(h, 0);
    CHECK(h.take(1).empty());   //fog of war: nothing about P1's ships reaches P2
}

TEST(wait_after_fleet_complete_if_opponent_not_done) {
    Harness h;
    joinBoth(h);
    h.take(0);
    placeFleet(h, 0);
    Lines v = h.take(0);
    CHECK(v.size() == 6);
    CHECK(v[4] == "PLACE_OK DESTROYER");
    CHECK(v[5] == "WAIT");
    CHECK(h.g.phase() == Phase::Placing);
}

TEST(start_when_both_fleets_placed) {
    Harness h;
    joinBoth(h);
    placeFleet(h, 0);
    h.take(0);
    h.take(1);
    placeFleet(h, 1);
    CHECK((h.take(0) == Lines{"START", "YOUR_TURN"}));
    Lines v = h.take(1);
    CHECK(v.size() == 7 && v[4] == "PLACE_OK DESTROYER" && v[5] == "START" && v[6] == "OPP_TURN");
    CHECK(h.g.phase() == Phase::Playing);
    CHECK(h.g.turn() == 0);
}

TEST(early_fleet_then_late_opponent_starts) {
    Harness h;
    h.connect(0);
    h.say(0, "HELLO Ann");
    placeFleet(h, 0);   //done before P2 even connects
    CHECK(h.g.phase() == Phase::WaitingForPlayers);
    h.connect(1);
    h.say(1, "HELLO Bob");
    CHECK(h.g.phase() == Phase::Placing);
    placeFleet(h, 1);
    CHECK(h.g.phase() == Phase::Playing);
    CHECK(h.last(0) == "YOUR_TURN");
}

TEST(place_during_play_rejected) {
    Harness h;
    startGame(h);
    h.say(0, "PLACE CARRIER J1 H");
    CHECK(h.last(0) == "ERR placing is over");
}

TEST(board_dumps_in_log_at_start) {
    Harness h;
    startGame(h);
    bool found = false;
    for (const std::string& l : h.log)
        if (l.find("board at start") != std::string::npos && l.find("A  C C C C C") != std::string::npos) found = true;
    CHECK(found);
}

//---------------------------------------------------------------- FIRE

TEST(fire_before_start_rejected) {
    Harness h;
    joinBoth(h);
    h.take(0);
    h.say(0, "FIRE A1");
    CHECK(h.last(0) == "ERR game has not started");
}

TEST(fire_out_of_turn_rejected) {
    Harness h;
    startGame(h);
    h.say(1, "FIRE A1");
    CHECK((h.take(1) == Lines{"ERR not your turn"}));
    CHECK(h.take(0).empty());
    CHECK(h.g.turn() == 0);
    CHECK(!h.g.board(0).firedAt(0, 0));
}

TEST(miss_swaps_turns) {
    Harness h;
    startGame(h);
    h.say(0, "FIRE B1");
    CHECK((h.take(0) == Lines{"RESULT B1 MISS", "OPP_TURN"}));
    CHECK((h.take(1) == Lines{"OPP_FIRED B1 MISS", "YOUR_TURN"}));
    CHECK(h.g.turn() == 1);
}

TEST(hit_also_swaps_turns) {
    Harness h;
    startGame(h);
    h.say(0, "FIRE A1");
    CHECK((h.take(0) == Lines{"RESULT A1 HIT", "OPP_TURN"}));
    CHECK((h.take(1) == Lines{"OPP_FIRED A1 HIT", "YOUR_TURN"}));
    CHECK(h.g.turn() == 1);
}

TEST(sunk_names_the_ship) {
    Harness h;
    startGame(h);
    h.say(0, "FIRE I1");
    h.say(1, "FIRE B1");
    h.say(0, "fire i2");
    Lines v = h.take(0);
    CHECK(v[v.size() - 2] == "RESULT I2 SUNK DESTROYER");
    CHECK(h.last(1) == "YOUR_TURN");
    CHECK(h.g.board(1).isSunk(shipIndex("DESTROYER")));
}

TEST(same_cell_twice_rejected_turn_kept) {
    Harness h;
    startGame(h);
    h.say(0, "FIRE B1");
    h.say(1, "FIRE B1");
    h.take(0);
    h.take(1);
    h.say(0, "FIRE B1");
    CHECK((h.take(0) == Lines{"ERR already fired at that cell"}));
    CHECK(h.take(1).empty());
    CHECK(h.g.turn() == 0);
}

TEST(bad_fire_syntax_rejected_turn_kept) {
    Harness h;
    startGame(h);
    const char* bad[] = {"FIRE", "FIRE A1 A2", "FIRE Z9", "FIRE A11", "FIRE 1A", "FIRE A0"};
    for (const char* line : bad) {
        h.say(0, line);
        CHECK(startsWith(h.last(0), "ERR "));
    }
    CHECK(h.take(0).size() == sizeof bad / sizeof bad[0]);
    CHECK(h.take(1).empty());
    CHECK(h.g.turn() == 0);
}

//P1 sinks every P2 ship while P2 only hits water
static void playToP1Win(Harness& h) {
    Lines targets = fleetCells();
    for (size_t i = 0; i < targets.size(); ++i) {
        h.say(0, "FIRE " + targets[i]);
        if (i + 1 < targets.size()) h.say(1, "FIRE " + waterCell((int)i));
    }
}

TEST(full_game_win_lose_then_reset) {
    Harness h;
    startGame(h);
    playToP1Win(h);
    Lines p1 = h.take(0), p2 = h.take(1);
    CHECK(p1[p1.size() - 2] == "RESULT I2 SUNK DESTROYER");
    CHECK(p1.back() == "GAME_OVER WIN");
    CHECK(p2[p2.size() - 2] == "OPP_FIRED I2 SUNK DESTROYER");
    CHECK(p2.back() == "GAME_OVER LOSE");
    CHECK((h.kicks == std::vector<int>{0, 1}));
    CHECK(h.g.phase() == Phase::WaitingForPlayers);
    CHECK(!h.g.connected(0) && !h.g.connected(1));
    CHECK(h.g.freeSlot() == 0);
}

TEST(lines_after_kick_are_ignored) {
    Harness h;
    startGame(h);
    playToP1Win(h);
    h.take(0);
    h.take(1);
    h.say(1, "FIRE A1");   //late bytes from a socket the server is closing
    h.leave(1);            //and a late disconnect
    CHECK(h.take(0).empty() && h.take(1).empty());
}

TEST(new_game_after_reset) {
    Harness h;
    startGame(h);
    playToP1Win(h);
    h.take(0);
    h.take(1);
    h.kicks.clear();
    startGame(h);
    CHECK(h.g.phase() == Phase::Playing);
    CHECK(!h.g.board(0).firedAt(1, 0));   //boards are fresh
    h.say(0, "FIRE A1");
    CHECK(h.take(0)[0] == "RESULT A1 HIT");
}

//---------------------------------------------------------------- disconnects

TEST(disconnect_mid_game_sends_opp_left) {
    Harness h;
    startGame(h);
    h.say(0, "FIRE B1");
    h.take(0);
    h.leave(1);
    CHECK((h.take(0) == Lines{"OPP_LEFT"}));
    CHECK((h.kicks == std::vector<int>{0}));
    CHECK(h.g.phase() == Phase::WaitingForPlayers);
    CHECK(h.g.freeSlot() == 0);
}

TEST(disconnect_during_placing_sends_opp_left) {
    Harness h;
    joinBoth(h);
    h.take(1);
    h.leave(0);
    CHECK((h.take(1) == Lines{"OPP_LEFT"}));
    CHECK((h.kicks == std::vector<int>{1}));
    CHECK(h.g.phase() == Phase::WaitingForPlayers);
}

TEST(disconnect_while_waiting_frees_only_that_slot) {
    Harness h;
    h.connect(0);
    h.say(0, "HELLO Ann");
    h.connect(1);   //connected but no HELLO yet
    h.take(0);
    h.leave(0);
    CHECK(h.kicks.empty());
    CHECK(h.take(1).empty());
    CHECK(h.g.connected(1));
    CHECK(h.g.freeSlot() == 0);
    //the next client gets slot 0, which is player 1
    h.connect(0);
    h.say(0, "HELLO Cat");
    CHECK(h.take(0)[0] == "WELCOME 1");
}

TEST(free_slot_reports_full) {
    Harness h;
    CHECK(h.g.freeSlot() == 0);
    h.connect(0);
    CHECK(h.g.freeSlot() == 1);
    h.connect(1);
    CHECK(h.g.freeSlot() == -1);
}

//---------------------------------------------------------------- turn timeout

TEST(timeout_off_by_default) {
    Harness h;
    h.tick(0);
    startGame(h);
    h.tick(1e9);
    CHECK(h.take(0).empty() && h.take(1).empty());
    CHECK(h.g.phase() == Phase::Playing);
}

TEST(stalled_player_loses_on_timeout) {
    Harness h(30);
    h.tick(100);
    startGame(h);
    h.tick(129);
    CHECK(h.take(0).empty());
    h.tick(130.5);
    CHECK((h.take(0) == Lines{"ERR turn timed out", "GAME_OVER LOSE"}));
    CHECK((h.take(1) == Lines{"GAME_OVER WIN"}));
    CHECK(h.g.phase() == Phase::WaitingForPlayers);
    CHECK(h.kicks.size() == 2);
}

TEST(timeout_restarts_every_turn) {
    Harness h(30);
    h.tick(0);
    startGame(h);
    h.tick(20);
    h.say(0, "FIRE B1");   //P2's turn starts at 20
    h.tick(45);
    CHECK(h.g.phase() == Phase::Playing);
    h.tick(50.5);
    CHECK(h.last(1) == "GAME_OVER LOSE");
    CHECK(h.last(0) == "GAME_OVER WIN");
}

TEST(no_timeout_while_placing) {
    Harness h(30);
    h.tick(0);
    joinBoth(h);
    h.tick(1000);
    CHECK(h.g.phase() == Phase::Placing);
}

int main() {
    int failedTests = 0;
    for (auto& t : registry()) {
        int before = g_failures;
        t.second();
        bool ok = g_failures == before;
        failedTests += !ok;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", t.first);
    }
    std::printf("\n%zu tests, %d failed\n", registry().size(), failedTests);
    return failedTests ? 1 : 0;
}
