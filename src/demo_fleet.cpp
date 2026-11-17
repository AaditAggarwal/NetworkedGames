#include "demo_fleet.h"

#include <random>

using p2p::GRID;
using p2p::ShotOutcome;

struct ShipType { const char* name; int len; };
static const ShipType TYPES[5] = {
    {"CARRIER", 5}, {"BATTLESHIP", 4}, {"CRUISER", 3}, {"SUBMARINE", 3}, {"DESTROYER", 2},
};

static void clear(int cells[GRID][GRID]) {
    for (int r = 0; r < GRID; ++r)
        for (int c = 0; c < GRID; ++c) cells[r][c] = -1;
}

DemoFleet::DemoFleet() {
    clear(cells_);
    for (int i = 0; i < SHIPS; ++i) tryPlace(i, i * 2, 0, false);
}

DemoFleet::DemoFleet(unsigned seed) {
    clear(cells_);
    std::mt19937 rng(seed);
    for (int i = 0; i < SHIPS; ++i)
        while (!tryPlace(i, (int)(rng() % GRID), (int)(rng() % GRID), rng() % 2 == 0)) {}
}

bool DemoFleet::tryPlace(int ship, int row, int col, bool vertical) {
    int len = TYPES[ship].len;
    for (int k = 0; k < len; ++k) {
        int r = row + (vertical ? k : 0), c = col + (vertical ? 0 : k);
        if (r >= GRID || c >= GRID || cells_[r][c] >= 0) return false;
    }
    for (int k = 0; k < len; ++k) cells_[row + (vertical ? k : 0)][col + (vertical ? 0 : k)] = ship;
    return true;
}

ShotOutcome DemoFleet::receiveShot(int row, int col) {
    ShotOutcome o;
    if (row < 0 || row >= GRID || col < 0 || col >= GRID) { o.kind = ShotOutcome::OutOfBounds; return o; }
    if (fired_[row][col]) { o.kind = ShotOutcome::AlreadyFired; return o; }
    fired_[row][col] = true;
    int ship = cells_[row][col];
    if (ship < 0) { o.kind = ShotOutcome::Miss; return o; }
    if (++hits_[ship] == TYPES[ship].len) {
        o.kind = ShotOutcome::Sunk;
        o.ship = TYPES[ship].name;
    } else {
        o.kind = ShotOutcome::Hit;
    }
    return o;
}

bool DemoFleet::allSunk() const {
    for (int i = 0; i < SHIPS; ++i)
        if (hits_[i] < TYPES[i].len) return false;
    return true;
}

std::string DemoFleet::render() const {
    std::string out = "   1 2 3 4 5 6 7 8 9 10\n";
    for (int r = 0; r < GRID; ++r) {
        out += (char)('A' + r);
        out += ' ';
        for (int c = 0; c < GRID; ++c) {
            int ship = cells_[r][c];
            char ch = '.';
            if (fired_[r][c]) ch = ship >= 0 ? 'X' : 'o';
            else if (ship >= 0) ch = TYPES[ship].name[0];
            out += ' ';
            out += ch;
        }
        out += '\n';
    }
    return out;
}
