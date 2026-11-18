//unit tests for common/rules and server/board
//no framework, each test registers itself and main() runs them all, nonzero exit if any check failed
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../common/rules.h"
#include "../server/board.h"

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

enum { CARRIER, BATTLESHIP, CRUISER, SUBMARINE, DESTROYER };

//a legal fleet: every ship horizontal, one per row, rows a, c, e, g, i
static Board fullBoard() {
    Board b;
    for (int i = 0; i < NUM_SHIPS; ++i) b.place(i, i * 2, 0, false);
    return b;
}

//rules

TEST(parse_cell_corners) {
    int r, c;
    CHECK(parseCell("A1", r, c) && r == 0 && c == 0);
    CHECK(parseCell("J10", r, c) && r == 9 && c == 9);
    CHECK(parseCell("b7", r, c) && r == 1 && c == 6);
}

TEST(parse_cell_rejects_bad_input) {
    int r, c;
    CHECK(!parseCell("", r, c));
    CHECK(!parseCell("A", r, c));
    CHECK(!parseCell("K1", r, c));
    CHECK(!parseCell("A0", r, c));
    CHECK(!parseCell("A11", r, c));
    CHECK(!parseCell("A1X", r, c));
    CHECK(!parseCell("1A", r, c));
    CHECK(!parseCell("A-1", r, c));
    CHECK(!parseCell("A100", r, c));
}

TEST(cell_name_round_trip) {
    for (int r = 0; r < GRID; ++r)
        for (int c = 0; c < GRID; ++c) {
            int r2, c2;
            CHECK(parseCell(cellName(r, c), r2, c2) && r2 == r && c2 == c);
        }
    CHECK(cellName(0, 0) == "A1");
    CHECK(cellName(9, 9) == "J10");
}

TEST(ship_index_lookup) {
    CHECK(shipIndex("CARRIER") == CARRIER);
    CHECK(shipIndex("destroyer") == DESTROYER);
    CHECK(shipIndex("Submarine") == SUBMARINE);
    CHECK(shipIndex("BOAT") == -1);
    CHECK(shipIndex("") == -1);
}

TEST(ship_lengths_total_17) {
    int total = 0;
    for (const ShipType& s : SHIPS) total += s.len;
    CHECK(total == 17);
}

TEST(parse_orientation) {
    bool v = true;
    CHECK(parseOrientation("H", v) && !v);
    CHECK(parseOrientation("v", v) && v);
    CHECK(!parseOrientation("X", v));
    CHECK(!parseOrientation("", v));
    CHECK(!parseOrientation("HV", v));
}

//placing

TEST(place_horizontal_extends_right) {
    Board b;
    CHECK(b.place(CARRIER, 0, 0, false) == PlaceError::None);
    for (int c = 0; c < 5; ++c) CHECK(b.shipAt(0, c) == CARRIER);
    CHECK(b.shipAt(0, 5) == -1);
    CHECK(b.shipAt(1, 0) == -1);
}

TEST(place_vertical_extends_down) {
    Board b;
    CHECK(b.place(DESTROYER, 3, 4, true) == PlaceError::None);
    CHECK(b.shipAt(3, 4) == DESTROYER);
    CHECK(b.shipAt(4, 4) == DESTROYER);
    CHECK(b.shipAt(5, 4) == -1);
    CHECK(b.shipAt(3, 5) == -1);
}

TEST(place_flush_with_edges_is_ok) {
    Board b;
    CHECK(b.place(CARRIER, 9, 5, false) == PlaceError::None);   //j6..j10
    CHECK(b.place(BATTLESHIP, 6, 9, true) == PlaceError::Overlap); //g10..j10 hits j10
    CHECK(b.place(BATTLESHIP, 5, 9, true) == PlaceError::None);    //f10..i10
    CHECK(b.place(DESTROYER, 0, 8, false) == PlaceError::None);    //a9..a10
    CHECK(b.place(CRUISER, 0, 0, true) == PlaceError::None);       //a1..c1
}

TEST(place_out_of_bounds) {
    Board b;
    CHECK(b.place(CARRIER, 0, 6, false) == PlaceError::OutOfBounds);  //a7..a11
    CHECK(b.place(CARRIER, 6, 0, true) == PlaceError::OutOfBounds);   //g1..k1
    CHECK(b.place(DESTROYER, -1, 0, false) == PlaceError::OutOfBounds);
    CHECK(b.place(DESTROYER, 0, 10, false) == PlaceError::OutOfBounds);
    CHECK(!b.isPlaced(CARRIER));
    CHECK(b.shipAt(0, 6) == -1);   //rejected placement left nothing behind
}

TEST(place_overlap_rejected_and_board_unchanged) {
    Board b;
    CHECK(b.place(CARRIER, 2, 0, false) == PlaceError::None);       //c1..c5
    CHECK(b.place(BATTLESHIP, 0, 3, true) == PlaceError::Overlap);  //a4..d4 crosses c4
    CHECK(!b.isPlaced(BATTLESHIP));
    CHECK(b.shipAt(0, 3) == -1);
    CHECK(b.shipAt(1, 3) == -1);
    CHECK(b.shipAt(2, 3) == CARRIER);
}

TEST(place_adjacent_is_ok) {
    Board b;
    CHECK(b.place(CARRIER, 0, 0, false) == PlaceError::None);
    CHECK(b.place(BATTLESHIP, 1, 0, false) == PlaceError::None);
    CHECK(b.place(DESTROYER, 0, 5, false) == PlaceError::None);
}

TEST(place_twice_rejected) {
    Board b;
    CHECK(b.place(CRUISER, 0, 0, false) == PlaceError::None);
    CHECK(b.place(CRUISER, 5, 5, false) == PlaceError::AlreadyPlaced);
    CHECK(b.shipAt(5, 5) == -1);
}

TEST(place_unknown_ship) {
    Board b;
    CHECK(b.place(-1, 0, 0, false) == PlaceError::UnknownShip);
    CHECK(b.place(NUM_SHIPS, 0, 0, false) == PlaceError::UnknownShip);
}

TEST(all_placed) {
    Board b;
    CHECK(!b.allPlaced());
    for (int i = 0; i < NUM_SHIPS - 1; ++i) b.place(i, i * 2, 0, false);
    CHECK(!b.allPlaced());
    b.place(NUM_SHIPS - 1, 9, 0, false);
    CHECK(b.allPlaced());
}

//firing

TEST(fire_miss) {
    Board b = fullBoard();
    ShotResult r = b.fire(1, 0);   //b1, row b is empty
    CHECK(r.shot == Shot::Miss && r.ship == -1 && r.ok());
    CHECK(b.firedAt(1, 0));
    CHECK(shotToken(r) == "MISS");
}

TEST(fire_hit_then_sunk) {
    Board b = fullBoard();
    ShotResult r1 = b.fire(8, 0);  //destroyer at i1..i2
    CHECK(r1.shot == Shot::Hit && r1.ship == DESTROYER);
    CHECK(shotToken(r1) == "HIT");
    CHECK(!b.isSunk(DESTROYER));
    ShotResult r2 = b.fire(8, 1);
    CHECK(r2.shot == Shot::Sunk && r2.ship == DESTROYER);
    CHECK(shotToken(r2) == "SUNK DESTROYER");
    CHECK(b.isSunk(DESTROYER));
    CHECK(b.shipsLeft() == NUM_SHIPS - 1);
}

TEST(fire_same_cell_twice_rejected) {
    Board b = fullBoard();
    CHECK(b.fire(0, 0).shot == Shot::Hit);
    ShotResult again = b.fire(0, 0);
    CHECK(again.shot == Shot::AlreadyFired && !again.ok());
    CHECK(b.fire(5, 5).shot == Shot::Miss);
    CHECK(b.fire(5, 5).shot == Shot::AlreadyFired);
}

TEST(repeat_hit_does_not_count_toward_sinking) {
    Board b = fullBoard();
    b.fire(8, 0);
    b.fire(8, 0);   //rejected; must not count as a second hit
    CHECK(!b.isSunk(DESTROYER));
    CHECK(b.fire(8, 1).shot == Shot::Sunk);
}

TEST(fire_out_of_bounds) {
    Board b = fullBoard();
    CHECK(b.fire(-1, 0).shot == Shot::OutOfBounds);
    CHECK(b.fire(0, 10).shot == Shot::OutOfBounds);
    CHECK(b.fire(10, 0).shot == Shot::OutOfBounds);
    CHECK(!b.fire(10, 10).ok());
}

TEST(sinking_every_ship_ends_game) {
    Board b = fullBoard();
    int sunk = 0;
    for (int r = 0; r < GRID; ++r)
        for (int c = 0; c < GRID; ++c) {
            CHECK(!b.allSunk());
            ShotResult res = b.fire(r, c);
            CHECK(res.ok());
            if (res.shot == Shot::Sunk) ++sunk;
            if (b.allSunk()) {
                //last ship is the destroyer on row i, sunk at i2
                CHECK(r == 8 && c == 1);
                CHECK(sunk == NUM_SHIPS);
                return;
            }
        }
    CHECK(false);   //never reached allSunk
}

TEST(seventeen_hits_win) {
    Board b = fullBoard();
    int hits = 0;
    for (int r = 0; r < GRID && !b.allSunk(); ++r)
        for (int c = 0; c < GRID; ++c)
            if (b.shipAt(r, c) >= 0) {
                b.fire(r, c);
                ++hits;
            }
    CHECK(hits == 17);
    CHECK(b.allSunk());
    CHECK(b.shipsLeft() == 0);
}

TEST(vertical_ship_sinks) {
    Board b;
    b.place(SUBMARINE, 4, 7, true);   //e8, f8, g8
    CHECK(b.fire(5, 7).shot == Shot::Hit);
    CHECK(b.fire(4, 7).shot == Shot::Hit);
    CHECK(b.fire(3, 7).shot == Shot::Miss);
    ShotResult r = b.fire(6, 7);
    CHECK(r.shot == Shot::Sunk && r.ship == SUBMARINE);
    CHECK(shotToken(r) == "SUNK SUBMARINE");
}

TEST(reset_clears_everything) {
    Board b = fullBoard();
    b.fire(0, 0);
    b.reset();
    CHECK(!b.allPlaced());
    CHECK(b.shipAt(0, 0) == -1);
    CHECK(!b.firedAt(0, 0));
    CHECK(b.place(CARRIER, 0, 0, false) == PlaceError::None);
}

TEST(error_texts_are_single_line) {
    PlaceError pe[] = {PlaceError::UnknownShip, PlaceError::AlreadyPlaced,
                       PlaceError::OutOfBounds, PlaceError::Overlap};
    for (PlaceError e : pe) CHECK(std::string(placeErrorText(e)).find('\n') == std::string::npos);
    CHECK(std::string(shotErrorText(Shot::AlreadyFired)) == "already fired at that cell");
}

TEST(render_shows_ships_hits_and_misses) {
    Board b;
    b.place(DESTROYER, 0, 0, false);
    b.fire(0, 0);
    b.fire(1, 1);
    std::string s = b.render();
    CHECK(s.find("A  X D") != std::string::npos);
    CHECK(s.find("B  . o") != std::string::npos);
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
