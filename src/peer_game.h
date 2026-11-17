//messages: HELLO <name>, READY, FIRE <cell>, RESULT <cell> HIT|MISS|SUNK <ship>, DEFEAT, ERR <reason>, BYE
#pragma once

#include <string>
#include <vector>

namespace p2p {

constexpr int GRID = 10;

enum class State { Connecting, Placing, WaitReady, MyTurn, WaitResult, PeerTurn, GameOver };
const char* stateName(State s);

enum class Mark : unsigned char { None, Hit, Miss, Sunk };

struct ShotOutcome {
    enum Kind { Miss, Hit, Sunk, OutOfBounds, AlreadyFired } kind = Miss;
    std::string ship;
};

class OwnBoard {
public:
    virtual ~OwnBoard() = default;
    virtual ShotOutcome receiveShot(int row, int col) = 0;
    virtual bool allSunk() const = 0;
};

bool parseCell(const std::string& s, int& row, int& col);
std::string cellName(int row, int col);

class PeerGame {
public:
    PeerGame(bool isHost, const std::string& myName, OwnBoard& board);

    void onConnected();
    void onAllPlaced();
    bool fire(int row, int col);
    void quit();

    void onLine(const std::string& line);
    void onDisconnect();

    std::vector<std::string> takeOutgoing();
    std::vector<std::string> takeEvents();

    State state() const { return state_; }
    bool isHost() const { return host_; }
    bool myTurn() const { return state_ == State::MyTurn; }
    bool peerReady() const { return peerReady_; }
    bool won() const { return won_; }
    bool peerLeft() const { return peerLeft_; }
    const std::string& peerName() const { return peerName_; }
    const std::string& lastError() const { return lastErr_; }
    Mark enemy(int row, int col) const { return enemy_[row][col]; }
    Mark onMe(int row, int col) const { return onMe_[row][col]; }
    const std::vector<std::string>& enemySunk() const { return enemySunk_; }
    int shotsFired() const { return shotsFired_; }
    int shotsHit() const { return shotsHit_; }

private:
    void send(const std::string& line);
    void event(const std::string& text);
    void reject(const std::string& reason);
    void maybeStart();
    void handleFire(const std::vector<std::string>& w);
    void handleResult(const std::vector<std::string>& w);
    void gameOver(bool won, const std::string& why);

    bool host_;
    std::string myName_, peerName_;
    OwnBoard& board_;
    State state_ = State::Connecting;
    bool myReady_ = false, peerReady_ = false;
    bool won_ = false, peerLeft_ = false;
    int pendingRow_ = -1, pendingCol_ = -1;
    Mark enemy_[GRID][GRID] = {};
    Mark onMe_[GRID][GRID] = {};
    std::vector<std::string> enemySunk_;
    int shotsFired_ = 0, shotsHit_ = 0;
    std::string lastErr_;
    std::vector<std::string> out_, events_;
};

}
