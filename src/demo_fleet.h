//temporary own board for console play and tests, replaced by Board from game-logic at integration
#pragma once

#include <string>

#include "peer_game.h"

class DemoFleet : public p2p::OwnBoard {
public:
    explicit DemoFleet(unsigned seed);
    DemoFleet();

    p2p::ShotOutcome receiveShot(int row, int col) override;
    bool allSunk() const override;
    std::string render() const;

private:
    static const int SHIPS = 5;
    bool tryPlace(int ship, int row, int col, bool vertical);
    int cells_[p2p::GRID][p2p::GRID];
    bool fired_[p2p::GRID][p2p::GRID] = {};
    int hits_[SHIPS] = {};
};
