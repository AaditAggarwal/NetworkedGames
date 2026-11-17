// state.cpp - client-side protocol handling. See PROTOCOL.md / plan section 2.
#include "state.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "../server/board.h"

static std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

// ---------------------------------------------------------------- queries

int ClientState::nextShipToPlace() const {
    for (int i = 0; i < NUM_SHIPS; ++i)
        if (!placed[i] && i != pendingShip) return i;
    return -1;
}

bool ClientState::allPlaced() const {
    for (int i = 0; i < NUM_SHIPS; ++i)
        if (!placed[i]) return false;
    return true;
}

static bool covers(const Placement& p, int len, int row, int col) {
    if (p.vertical) return col == p.col && row >= p.row && row < p.row + len;
    return row == p.row && col >= p.col && col < p.col + len;
}

int ClientState::shipAt(int row, int col) const {
    for (int i = 0; i < NUM_SHIPS; ++i)
        if (placed[i] && covers(fleet[i], SHIPS[i].len, row, col)) return i;
    return -1;
}

//rebuild our fleet on a Board and ask it, so the preview uses the exact rules the server checks
bool ClientState::fits(int ship, int row, int col, bool vert) const {
    Board b;
    for (int i = 0; i < NUM_SHIPS; ++i)
        if (placed[i]) b.place(i, fleet[i].row, fleet[i].col, fleet[i].vertical);
    if (pendingShip >= 0) b.place(pendingShip, pendingPlace.row, pendingPlace.col, pendingPlace.vertical);
    return b.place(ship, row, col, vert) == PlaceError::None;
}

int ClientState::enemyShipsLeft() const {
    int n = 0;
    for (bool s : enemySunk) n += !s;
    return n;
}

int ClientState::myShipsLeft() const {
    int n = 0;
    for (bool s : sunk) n += !s;
    return n;
}

// ---------------------------------------------------------------- updates

void ClientState::addLog(bool outgoing, const std::string& text) {
    log.push_back({outgoing, text});
    while (log.size() > 200) log.pop_front();
    std::printf("%s %s\n", outgoing ? ">" : "<", text.c_str());
    std::fflush(stdout);
}

void ClientState::setNotice(const std::string& text, double now, bool error) {
    notice = text;
    noticeError = error;
    noticeTime = now;
}

void ClientState::resetSession() {
    ClientState fresh;
    fresh.host = host;
    fresh.port = port;
    fresh.name = name;
    fresh.rawMode = rawMode;
    fresh.log = log;
    *this = fresh;
}

// When the server says SUNK we only know the final cell and the ship type.
// If the hits around that cell line up into exactly one run of the right
// length, mark that whole run as sunk; otherwise just the cell itself.
void ClientState::markEnemySunk(int row, int col, int ship) {
    enemy[row][col] = Mark::Sunk;
    if (ship < 0) return;
    enemySunk[ship] = true;
    int len = SHIPS[ship].len;

    auto isHit = [&](int r, int c) {
        return r >= 0 && r < GRID && c >= 0 && c < GRID &&
               (enemy[r][c] == Mark::Hit || (r == row && c == col));
    };
    int candidates = 0, bestDr = 0, bestDc = 0, bestStart = 0;
    const int dirs[2][2] = {{0, 1}, {1, 0}};
    for (auto& d : dirs) {
        // every window of `len` cells along this axis that contains (row,col)
        for (int s = -(len - 1); s <= 0; ++s) {
            bool ok = true;
            for (int k = 0; k < len && ok; ++k)
                ok = isHit(row + d[0] * (s + k), col + d[1] * (s + k));
            if (ok) { ++candidates; bestDr = d[0]; bestDc = d[1]; bestStart = s; }
        }
    }
    if (candidates != 1) return;
    for (int k = 0; k < len; ++k)
        enemy[row + bestDr * (bestStart + k)][col + bestDc * (bestStart + k)] = Mark::Sunk;
}

void ClientState::handleLine(const std::string& line, double now) {
    addLog(false, line);
    std::vector<std::string> w = split(line);
    if (w.empty()) return;
    const std::string& cmd = w[0];

    if (cmd == "WELCOME" && w.size() >= 2) {
        playerNum = std::atoi(w[1].c_str());
        phase = Phase::Placing;
    } else if (cmd == "WAIT") {
        waiting = true;
    } else if (cmd == "PLACE_OK" && w.size() >= 2) {
        int i = shipIndex(w[1]);
        if (i >= 0) {
            fleet[i] = (i == pendingShip) ? pendingPlace : fleet[i];
            placed[i] = true;
        }
        if (i == pendingShip) pendingShip = -1;
        waiting = false;
    } else if (cmd == "ERR") {
        std::string reason = line.size() > 4 ? line.substr(4) : "error";
        setNotice("Server: " + reason, now, true);
        pendingShip = -1;              // a rejected PLACE or FIRE is simply dropped
        pendingRow = pendingCol = -1;
    } else if (cmd == "START") {
        phase = Phase::Playing;
        waiting = false;
        pendingShip = -1;
    } else if (cmd == "YOUR_TURN") {
        if (phase == Phase::Placing) phase = Phase::Playing;
        myTurn = true;
        turnKnown = true;
    } else if (cmd == "OPP_TURN") {
        if (phase == Phase::Placing) phase = Phase::Playing;
        myTurn = false;
        turnKnown = true;
    } else if (cmd == "RESULT" && w.size() >= 3) {
        int r, c;
        if (parseCell(w[1], r, c)) {
            ++shotsFired;
            if (w[2] == "MISS") {
                enemy[r][c] = Mark::Miss;
            } else {
                ++shotsHit;
                enemy[r][c] = Mark::Hit;
                if (w[2] == "SUNK") {
                    int ship = w.size() >= 4 ? shipIndex(w[3]) : -1;
                    markEnemySunk(r, c, ship);
                    setNotice(std::string("You sank their ") + (ship >= 0 ? SHIPS[ship].name : "ship") + "!", now);
                }
            }
        }
        pendingRow = pendingCol = -1;
    } else if (cmd == "OPP_FIRED" && w.size() >= 3) {
        int r, c;
        if (parseCell(w[1], r, c)) {
            if (w[2] == "MISS") {
                onMe[r][c] = Mark::Miss;
            } else {
                onMe[r][c] = Mark::Hit;
                if (w[2] == "SUNK") {
                    int ship = w.size() >= 4 ? shipIndex(w[3]) : shipAt(r, c);
                    if (ship >= 0) {
                        sunk[ship] = true;
                        setNotice(std::string("They sank your ") + SHIPS[ship].name + "!", now);
                    }
                }
            }
        }
    } else if (cmd == "GAME_OVER" && w.size() >= 2) {
        phase = Phase::GameOver;
        won = (w[1] == "WIN");
        myTurn = false;
    } else if (cmd == "OPP_LEFT") {
        oppLeft = true;
        phase = Phase::GameOver;
        myTurn = false;
    }
    // Anything else is shown in the log strip but otherwise ignored.
}
