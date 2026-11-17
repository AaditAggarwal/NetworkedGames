//see board.h
#include "board.h"

static bool inBounds(int row, int col) {
    return row >= 0 && row < GRID && col >= 0 && col < GRID;
}

void Board::reset() {
    for (int r = 0; r < GRID; ++r)
        for (int c = 0; c < GRID; ++c) {
            cells_[r][c] = -1;
            fired_[r][c] = false;
        }
    for (int i = 0; i < NUM_SHIPS; ++i) {
        placed_[i] = false;
        hits_[i] = 0;
    }
}

//placing

PlaceError Board::place(int ship, int row, int col, bool vertical) {
    if (ship < 0 || ship >= NUM_SHIPS) return PlaceError::UnknownShip;
    if (placed_[ship]) return PlaceError::AlreadyPlaced;

    int len = SHIPS[ship].len;
    //check every cell first so a rejected placement leaves the board untouched
    for (int k = 0; k < len; ++k) {
        int r = row + (vertical ? k : 0);
        int c = col + (vertical ? 0 : k);
        if (!inBounds(r, c)) return PlaceError::OutOfBounds;
        if (cells_[r][c] >= 0) return PlaceError::Overlap;
    }
    for (int k = 0; k < len; ++k)
        cells_[row + (vertical ? k : 0)][col + (vertical ? 0 : k)] = ship;
    placed_[ship] = true;
    return PlaceError::None;
}

bool Board::isPlaced(int ship) const {
    return ship >= 0 && ship < NUM_SHIPS && placed_[ship];
}

bool Board::allPlaced() const {
    for (bool p : placed_)
        if (!p) return false;
    return true;
}

//playing

ShotResult Board::fire(int row, int col) {
    ShotResult res;
    if (!inBounds(row, col)) { res.shot = Shot::OutOfBounds; return res; }
    if (fired_[row][col]) { res.shot = Shot::AlreadyFired; return res; }

    fired_[row][col] = true;
    int ship = cells_[row][col];
    if (ship < 0) { res.shot = Shot::Miss; return res; }

    res.ship = ship;
    res.shot = (++hits_[ship] == SHIPS[ship].len) ? Shot::Sunk : Shot::Hit;
    return res;
}

bool Board::isSunk(int ship) const {
    return ship >= 0 && ship < NUM_SHIPS && placed_[ship] && hits_[ship] == SHIPS[ship].len;
}

bool Board::allSunk() const {
    return shipsLeft() == 0;
}

int Board::shipsLeft() const {
    int n = 0;
    for (int i = 0; i < NUM_SHIPS; ++i) n += placed_[i] && !isSunk(i);
    return n;
}

//inspection

int Board::shipAt(int row, int col) const {
    return inBounds(row, col) ? cells_[row][col] : -1;
}

bool Board::firedAt(int row, int col) const {
    return inBounds(row, col) && fired_[row][col];
}

//ships show as the first letter of their name, X = hit, o = miss, . = water
std::string Board::render() const {
    std::string out = "   1 2 3 4 5 6 7 8 9 10\n";
    for (int r = 0; r < GRID; ++r) {
        out += (char)('A' + r);
        out += ' ';
        for (int c = 0; c < GRID; ++c) {
            int ship = cells_[r][c];
            char ch = '.';
            if (fired_[r][c]) ch = ship >= 0 ? 'X' : 'o';
            else if (ship >= 0) ch = SHIPS[ship].name[0];
            out += ' ';
            out += ch;
        }
        out += '\n';
    }
    return out;
}

//protocol text

const char* placeErrorText(PlaceError e) {
    switch (e) {
    case PlaceError::None:          return "ok";
    case PlaceError::UnknownShip:   return "unknown ship";
    case PlaceError::AlreadyPlaced: return "ship already placed";
    case PlaceError::OutOfBounds:   return "ship does not fit on the board";
    case PlaceError::Overlap:       return "ship overlaps another ship";
    }
    return "bad placement";
}

const char* shotErrorText(Shot s) {
    switch (s) {
    case Shot::OutOfBounds:  return "cell is off the board";
    case Shot::AlreadyFired: return "already fired at that cell";
    default:                 return "ok";
    }
}

std::string shotToken(const ShotResult& r) {
    switch (r.shot) {
    case Shot::Miss: return "MISS";
    case Shot::Hit:  return "HIT";
    case Shot::Sunk: return std::string("SUNK ") + SHIPS[r.ship].name;
    default:         return "";
    }
}
