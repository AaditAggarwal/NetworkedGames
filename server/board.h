//one player's 10x10 board, their fleet and the shots fired at it
//pure game logic with no sockets, the server keeps one per player (usage in server/README.md)
#pragma once

#include <string>

#include "../common/rules.h"

enum class PlaceError { None, UnknownShip, AlreadyPlaced, OutOfBounds, Overlap };

enum class Shot { Miss, Hit, Sunk, OutOfBounds, AlreadyFired };

struct ShotResult {
    Shot shot = Shot::Miss;
    int ship = -1;    //ship that was hit or sunk, -1 on a miss or error
    bool ok() const { return shot == Shot::Miss || shot == Shot::Hit || shot == Shot::Sunk; }
};

class Board {
public:
    Board() { reset(); }
    void reset();

    //placing
    PlaceError place(int ship, int row, int col, bool vertical);
    bool isPlaced(int ship) const;
    bool allPlaced() const;

    //playing
    //a rejected shot changes nothing so the server can send ERR and keep the same turn
    ShotResult fire(int row, int col);
    bool isSunk(int ship) const;
    bool allSunk() const;
    int shipsLeft() const;

    //inspection for the server log and tests
    int shipAt(int row, int col) const;    //ship covering the cell, or -1
    bool firedAt(int row, int col) const;
    std::string render() const;           //ascii picture of the board

private:
    int cells_[GRID][GRID];               //ship index or -1
    bool fired_[GRID][GRID];
    bool placed_[NUM_SHIPS];
    int hits_[NUM_SHIPS];
};

//protocol text helpers
const char* placeErrorText(PlaceError e);   //text after "ERR " for a bad PLACE
const char* shotErrorText(Shot s);          //text after "ERR " for a bad FIRE
std::string shotToken(const ShotResult& r); //"MISS", "HIT", or "SUNK CARRIER"
