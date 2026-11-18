//battleship rules shared by the server and the client
//cells: letter is the row (a..j top to bottom), number is the column (1..10), a1 is top-left
//PLACE cell is the ship's top-left end, H extends right and V extends down
#pragma once

#include <string>

constexpr int GRID = 10;
constexpr int NUM_SHIPS = 5;

struct ShipType { const char* name; int len; };
extern const ShipType SHIPS[NUM_SHIPS];

int shipIndex(const std::string& name);    //-1 if unknown, case-insensitive

bool parseCell(const std::string& s, int& row, int& col);
std::string cellName(int row, int col);

//"H" or "V", case-insensitive, false for anything else
bool parseOrientation(const std::string& s, bool& vertical);
