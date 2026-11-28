//see board.h
#include "board.h"

//true if the cell is on the 10x10 grid
static bool inBounds(int row, int col) {
    return row >= 0 && row < GRID && col >= 0 && col < GRID;
}

//empty board, no ships placed and no shots fired
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

//puts a ship with its top-left end at (row, col), going right or down
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
    //all cells are free so mark them with this ship
    for (int k = 0; k < len; ++k)
        cells_[row + (vertical ? k : 0)][col + (vertical ? 0 : k)] = ship;
    placed_[ship] = true;
    return PlaceError::None;
}

//true if this ship is already on the board
bool Board::isPlaced(int ship) const {
    return ship >= 0 && ship < NUM_SHIPS && placed_[ship];
}

//true once all 5 ships are placed, the server can send START then
bool Board::allPlaced() const {
    for (bool p : placed_)
        if (!p) return false;
    return true;
}

//playing

//fires at a cell and says miss, hit or sunk, or why the shot was rejected
ShotResult Board::fire(int row, int col) {
    ShotResult res;
    //rejected shots return early before anything is marked
    if (!inBounds(row, col)) { res.shot = Shot::OutOfBounds; return res; }
    if (fired_[row][col]) { res.shot = Shot::AlreadyFired; return res; }

    fired_[row][col] = true;
    int ship = cells_[row][col];
    if (ship < 0) { res.shot = Shot::Miss; return res; }

    //a ship sinks when its hit count reaches its length
    res.ship = ship;
    res.shot = (++hits_[ship] == SHIPS[ship].len) ? Shot::Sunk : Shot::Hit;
    return res;
}

//true if every cell of this ship has been hit
bool Board::isSunk(int ship) const {
    return ship >= 0 && ship < NUM_SHIPS && placed_[ship] && hits_[ship] == SHIPS[ship].len;
}

//true when no ships are left, the shooter wins
bool Board::allSunk() const {
    return shipsLeft() == 0;
}

//number of placed ships that are still afloat
int Board::shipsLeft() const {
    int n = 0;
    for (int i = 0; i < NUM_SHIPS; ++i) n += placed_[i] && !isSunk(i);
    return n;
}

//inspection

//ship covering the cell, -1 for water or off the board
int Board::shipAt(int row, int col) const {
    return inBounds(row, col) ? cells_[row][col] : -1;
}

//true if this cell was already shot at
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

//reason sent after "ERR " when a PLACE is rejected
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

//reason sent after "ERR " when a FIRE is rejected
const char* shotErrorText(Shot s) {
    switch (s) {
    case Shot::OutOfBounds:  return "cell is off the board";
    case Shot::AlreadyFired: return "already fired at that cell";
    default:                 return "ok";
    }
}

//result part of RESULT and OPP_FIRED, the ship name only goes with SUNK
std::string shotToken(const ShotResult& r) {
    switch (r.shot) {
    case Shot::Miss: return "MISS";
    case Shot::Hit:  return "HIT";
    case Shot::Sunk: return std::string("SUNK ") + SHIPS[r.ship].name;
    default:         return "";
    }
}
