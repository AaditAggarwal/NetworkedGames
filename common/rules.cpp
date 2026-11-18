//see rules.h
#include "rules.h"

#include <cctype>

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

bool parseOrientation(const std::string& s, bool& vertical) {
    std::string o = upper(s);
    if (o == "H") { vertical = false; return true; }
    if (o == "V") { vertical = true; return true; }
    return false;
}
