// state.cpp - client-side protocol handling. See PROTOCOL.md / plan section 2.
#include "state.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

const ShipType SHIPS[NUM_SHIPS] = {
    {"CARRIER", 5}, {"BATTLESHIP", 4}, {"CRUISER", 3}, {"SUBMARINE", 3}, {"DESTROYER", 2},
};

static std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

int shipIndex(const std::string& name) {
    std::string n = upper(name);
    for (int i = 0; i < NUM_SHIPS; ++i)
        if (n == SHIPS[i].name) return i;
    return -1;
}

bool parseCell(const std::string& s, int& row, int& col) {
    if (s.size() < 2 || s.size() > 3) return false;
    char r = (char)std::toupper((unsigned char)s[0]);
    if (r < 'A' || r >= 'A' + GRID) return false;
    int c = 0;
    for (size_t i = 1; i < s.size(); ++i) {
        if (!std::isdigit((unsigned char)s[i])) return false;
        c = c * 10 + (s[i] - '0');
    }
    if (c < 1 || c > GRID) return false;
    row = r - 'A';
    col = c - 1;
    return true;
}

std::string cellName(int row, int col) {
    return std::string(1, (char)('A' + row)) + std::to_string(col + 1);
}

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

bool ClientState::fits(int ship, int row, int col, bool vert) const {
    int len = SHIPS[ship].len;
    for (int k = 0; k < len; ++k) {
        int r = row + (vert ? k : 0);
        int c = col + (vert ? 0 : k);
        if (r < 0 || r >= GRID || c < 0 || c >= GRID) return false;
        if (shipAt(r, c) >= 0) return false;
        if (pendingShip >= 0 && covers(pendingPlace, SHIPS[pendingShip].len, r, c)) return false;
    }
    return true;
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
