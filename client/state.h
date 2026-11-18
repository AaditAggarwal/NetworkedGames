// state.h - everything the client knows. Updated only by server messages
// (handleLine) and by the local user's placement / firing intents.
//
// Fog of war: the client never learns where enemy ships are. The enemy grid
// only holds what RESULT messages told us.
#pragma once

#include <deque>
#include <string>

#include "../common/rules.h"   //GRID, SHIPS, shipIndex, parseCell, cellName (shared with the server)

enum class Phase {
    Connect,     // connect screen (typing host / port / name)
    Connecting,  // TCP connect in progress, or waiting for WELCOME
    Placing,     // placing our fleet
    Playing,     // START received
    GameOver     // GAME_OVER, OPP_LEFT, or connection lost
};

enum class Mark : unsigned char { None, Hit, Miss, Sunk };

struct Placement { int row = 0, col = 0; bool vertical = false; };

struct LogLine { bool outgoing; std::string text; };

struct ClientState {
    // ---- connect screen fields ----
    std::string host = "127.0.0.1";
    std::string port = "5000";
    std::string name = "Player";
    int focus = 0;                 // 0 host, 1 port, 2 name
    bool wantHost = false;         // HOST pressed, main.cpp starts the server
    bool hosting = false;          // this window also runs the game server
    std::string joinAt;            // "192.168.1.5:5000", shown so the other player knows where to join

    // ---- session ----
    Phase phase = Phase::Connect;
    std::string peer;              // "ip:port" of the server
    int playerNum = 0;
    bool waiting = false;          // got WAIT
    bool myTurn = false;
    bool turnKnown = false;
    bool won = false;
    bool oppLeft = false;
    bool connLost = false;
    std::string notice;            // last ERR reason / info for the banner
    double noticeTime = -100.0;
    bool noticeError = false;
    bool rawMode = false;          // F1: skip client-side checks (demo ERR handling)

    // ---- our fleet ----
    bool placed[NUM_SHIPS] = {};
    bool sunk[NUM_SHIPS] = {};
    Placement fleet[NUM_SHIPS];
    int pendingShip = -1;          // PLACE sent, waiting for PLACE_OK / ERR
    Placement pendingPlace;
    bool vertical = false;         // current rotation for the ghost ship
    Mark onMe[GRID][GRID] = {};    // opponent shots at us

    // ---- enemy waters ----
    Mark enemy[GRID][GRID] = {};
    bool enemySunk[NUM_SHIPS] = {};
    int pendingRow = -1, pendingCol = -1;   // FIRE sent, waiting for RESULT
    int shotsFired = 0, shotsHit = 0;

    // ---- protocol log strip ----
    std::deque<LogLine> log;

    // queries
    int nextShipToPlace() const;           // -1 when all placed
    bool allPlaced() const;
    int shipAt(int row, int col) const;    // index of our ship covering cell, or -1
    bool fits(int ship, int row, int col, bool vertical) const;   // in bounds, no overlap
    int enemyShipsLeft() const;
    int myShipsLeft() const;

    // updates
    void handleLine(const std::string& line, double now);
    void addLog(bool outgoing, const std::string& text);
    void setNotice(const std::string& text, double now, bool error = false);
    void resetSession();                   // back to a fresh game, keeps connect fields

private:
    void markEnemySunk(int row, int col, int ship);
};
