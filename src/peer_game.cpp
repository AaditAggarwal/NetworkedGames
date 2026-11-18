#include "peer_game.h"

#include <cctype>
#include <sstream>

namespace p2p {

const char* stateName(State s) {
    switch (s) {
    case State::Connecting: return "CONNECTING";
    case State::Placing:    return "PLACING";
    case State::WaitReady:  return "WAIT_READY";
    case State::MyTurn:     return "MY_TURN";
    case State::WaitResult: return "WAIT_RESULT";
    case State::PeerTurn:   return "PEER_TURN";
    case State::GameOver:   return "GAME_OVER";
    }
    return "?";
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

PeerGame::PeerGame(bool isHost, const std::string& myName, OwnBoard& board)
    : host_(isHost), myName_(myName.empty() ? "Player" : myName), board_(board) {
    for (char& c : myName_)
        if (c == ' ') c = '_';
}

void PeerGame::send(const std::string& line) { out_.push_back(line); }
void PeerGame::event(const std::string& text) { events_.push_back(text); }

void PeerGame::reject(const std::string& reason) {
    send("ERR " + reason);
    event("sent ERR: " + reason);
}

std::vector<std::string> PeerGame::takeOutgoing() {
    std::vector<std::string> v;
    v.swap(out_);
    return v;
}

std::vector<std::string> PeerGame::takeEvents() {
    std::vector<std::string> v;
    v.swap(events_);
    return v;
}

void PeerGame::gameOver(bool won, const std::string& why) {
    state_ = State::GameOver;
    won_ = won;
    event(why);
}


void PeerGame::onConnected() {
    if (state_ != State::Connecting) return;
    send("HELLO " + myName_);
    state_ = State::Placing;
}

void PeerGame::onAllPlaced() {
    if (state_ != State::Placing) return;
    myReady_ = true;
    send("READY");
    state_ = State::WaitReady;
    if (!peerReady_) event("waiting for the opponent to place their ships");
    maybeStart();
}

void PeerGame::maybeStart() {
    if (state_ != State::WaitReady || !myReady_ || !peerReady_) return;
    state_ = host_ ? State::MyTurn : State::PeerTurn;   //host fires first
    event(host_ ? "battle starts, your turn" : "battle starts, opponent fires first");
}

bool PeerGame::fire(int row, int col) {
    if (state_ == State::Placing) { event("type ready first"); return false; }
    if (state_ == State::WaitReady) { event("waiting for the opponent to type ready"); return false; }
    if (state_ != State::MyTurn) { event("not your turn"); return false; }
    if (row < 0 || row >= GRID || col < 0 || col >= GRID) return false;
    if (enemy_[row][col] != Mark::None) { event("you already fired at " + cellName(row, col)); return false; }
    pendingRow_ = row;
    pendingCol_ = col;
    send("FIRE " + cellName(row, col));
    state_ = State::WaitResult;
    return true;
}

void PeerGame::quit() {
    if (state_ != State::GameOver) send("BYE");
    state_ = State::GameOver;
}


void PeerGame::onLine(const std::string& line) {
    std::vector<std::string> w = split(line);
    if (w.empty()) return;
    const std::string& cmd = w[0];

    if (cmd == "ERR") {   //never answer an ERR, that could loop forever
        lastErr_ = line.size() > 4 ? line.substr(4) : "error";
        event("opponent says: " + lastErr_);
        if (state_ == State::WaitResult) {
            pendingRow_ = pendingCol_ = -1;
            state_ = State::MyTurn;
        }
        return;
    }
    if (state_ == State::GameOver) return;

    if (cmd == "HELLO") {
        if (w.size() < 2) return reject("HELLO needs a name");
        peerName_ = w[1];
        event(peerName_ + " connected");
    } else if (cmd == "READY") {
        if (state_ != State::Placing && state_ != State::WaitReady) return reject("unexpected READY");
        if (peerReady_) return;
        peerReady_ = true;
        event("opponent is ready");
        maybeStart();
    } else if (cmd == "FIRE") {
        handleFire(w);
    } else if (cmd == "RESULT") {
        handleResult(w);
    } else if (cmd == "DEFEAT") {
        gameOver(true, "you won, all enemy ships sunk");
    } else if (cmd == "BYE") {
        peerLeft_ = true;
        gameOver(false, "opponent left");
    } else {
        reject("unknown message " + cmd);
    }
}

void PeerGame::handleFire(const std::vector<std::string>& w) {
    if (state_ != State::PeerTurn) return reject("not your turn");
    int r, c;
    if (w.size() < 2 || !parseCell(w[1], r, c)) return reject("bad cell");

    ShotOutcome o = board_.receiveShot(r, c);
    if (o.kind == ShotOutcome::OutOfBounds) return reject("cell is off the board");
    if (o.kind == ShotOutcome::AlreadyFired) return reject("already fired at that cell");

    std::string cell = cellName(r, c);
    std::string token = o.kind == ShotOutcome::Miss ? "MISS" : o.kind == ShotOutcome::Hit ? "HIT" : "SUNK " + o.ship;
    onMe_[r][c] = o.kind == ShotOutcome::Miss ? Mark::Miss : o.kind == ShotOutcome::Hit ? Mark::Hit : Mark::Sunk;
    send("RESULT " + cell + " " + token);
    event("opponent fired at " + cell + ": " + token);

    if (board_.allSunk()) {
        send("DEFEAT");
        gameOver(false, "you lost, all your ships are sunk");
    } else {
        state_ = State::MyTurn;
    }
}

void PeerGame::handleResult(const std::vector<std::string>& w) {
    if (state_ != State::WaitResult) return reject("unexpected RESULT");
    int r, c;
    if (w.size() < 3 || !parseCell(w[1], r, c) || r != pendingRow_ || c != pendingCol_)
        return reject("RESULT does not match my FIRE");

    const std::string& kind = w[2];
    if (kind == "MISS") {
        enemy_[r][c] = Mark::Miss;
    } else if (kind == "HIT") {
        enemy_[r][c] = Mark::Hit;
        ++shotsHit_;
    } else if (kind == "SUNK") {
        enemy_[r][c] = Mark::Sunk;
        ++shotsHit_;
        enemySunk_.push_back(w.size() >= 4 ? w[3] : "SHIP");
    } else {
        return reject("RESULT must be HIT, MISS or SUNK");
    }
    ++shotsFired_;
    pendingRow_ = pendingCol_ = -1;
    event("your shot at " + cellName(r, c) + ": " + kind + (kind == "SUNK" ? " " + enemySunk_.back() : ""));
    state_ = State::PeerTurn;
}

void PeerGame::onDisconnect() {
    if (state_ == State::GameOver) return;
    peerLeft_ = true;
    gameOver(false, "peer left");
}

}
