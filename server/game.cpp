//see game.h
#include "game.h"

#include <cctype>
#include <sstream>

const char* phaseName(Phase p) {
    switch (p) {
    case Phase::WaitingForPlayers: return "WAITING_FOR_PLAYERS";
    case Phase::Placing:           return "PLACING";
    case Phase::Playing:           return "PLAYING";
    case Phase::GameOver:          return "GAME_OVER";
    }
    return "?";
}

static std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

//splits on any whitespace, so a trailing \r from nc or Windows is dropped too
static std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

Game::Game(double turnTimeoutSec) : timeout_(turnTimeoutSec > 0 ? turnTimeoutSec : 0) {}

// output queues

void Game::send(int player, const std::string& text) {
    if (connected(player)) out_.push_back({player, text});
}

void Game::err(int player, const std::string& reason) {
    send(player, "ERR " + reason);
}

void Game::log(const std::string& text) { log_.push_back(text); }

std::vector<OutMsg> Game::takeOutgoing() {
    std::vector<OutMsg> v;
    v.swap(out_);
    return v;
}

std::vector<int> Game::takeKicks() {
    std::vector<int> v;
    v.swap(kicks_);
    return v;
}

std::vector<std::string> Game::takeLog() {
    std::vector<std::string> v;
    v.swap(log_);
    return v;
}

std::string Game::who(int player) const {
    const Player& p = p_[player];
    std::string s = "P" + std::to_string(player + 1);
    if (!p.name.empty()) s += " " + p.name;
    if (!p.addr.empty()) s += " (" + p.addr + ")";
    return s;
}

void Game::setPhase(Phase next) {
    if (next == phase_) return;
    log(std::string("state ") + phaseName(phase_) + " -> " + phaseName(next));
    phase_ = next;
}

int Game::freeSlot() const {
    for (int i = 0; i < 2; ++i)
        if (!p_[i].connected) return i;
    return -1;
}

// server loop events

void Game::onConnect(int player, const std::string& addr) {
    if (!valid(player) || p_[player].connected) return;
    p_[player] = Player{};
    p_[player].connected = true;
    p_[player].addr = addr;
    log(who(player) + " connected, waiting for HELLO");
}

void Game::onDisconnect(int player) {
    if (!connected(player)) return;
    log(who(player) + " disconnected during " + phaseName(phase_));

    if (phase_ == Phase::WaitingForPlayers) {
        //no game yet, just free the slot, whoever else is here keeps waiting
        p_[player] = Player{};
        return;
    }

    //a game was set up, so it cannot go on with one player
    int other = 1 - player;
    p_[player].connected = false;
    if (connected(other)) {
        send(other, "OPP_LEFT");
        kicks_.push_back(other);
        log(who(other) + " told OPP_LEFT");
    }
    setPhase(Phase::GameOver);
    reset();
}

void Game::tick(double now) {
    now_ = now;
    if (phase_ != Phase::Playing || timeout_ <= 0) return;
    if (now_ - turnStart_ < timeout_) return;

    int loser = turn_;
    err(loser, "turn timed out");
    log(who(loser) + " timed out after " + std::to_string((int)timeout_) + "s");
    endGame(1 - loser, "turn timeout");
}

void Game::onLine(int player, const std::string& line) {
    if (!connected(player)) return;   //a kicked socket's leftover bytes
    std::vector<std::string> w = split(line);
    if (w.empty()) return;            //blank line, e.g. Enter pressed in nc

    std::string cmd = upper(w[0]);
    if (cmd == "HELLO") return handleHello(player, w);
    if (!p_[player].joined) return err(player, "say HELLO <name> first");
    if (cmd == "PLACE") return handlePlace(player, w);
    if (cmd == "FIRE") return handleFire(player, w);
    err(player, "unknown command " + w[0]);
}

// Handling Hello from clients
void Game::handleHello(int player, const std::vector<std::string>& w) {
    Player& me = p_[player];
    if (me.joined) return err(player, "already said HELLO");
    if (w.size() < 2) return err(player, "usage: HELLO <name>");

    //the name is one protocol token, so "HELLO Bob Smith" becomes Bob_Smith
    std::string name = w[1];
    for (size_t i = 2; i < w.size(); ++i) name += "_" + w[i];
    if (name.size() > MAX_NAME) name.resize(MAX_NAME);

    me.name = name;
    me.joined = true;
    log(who(player) + " joined");
    send(player, "WELCOME " + std::to_string(player + 1));

    if (joined(1 - player)) {
        setPhase(Phase::Placing);
        maybeStart();   //the other player may have placed their whole fleet while waiting
    } else {
        send(player, "WAIT");
    }
}

// PLACE

void Game::handlePlace(int player, const std::vector<std::string>& w) {
    //placing is allowed while still waiting for the opponent, the client shows the placing
    //screen right after WELCOME
    if (phase_ != Phase::WaitingForPlayers && phase_ != Phase::Placing)
        return err(player, "placing is over");
    if (w.size() != 4) return err(player, "usage: PLACE <ship> <cell> <H|V>");

    int ship = shipIndex(w[1]);
    int row, col;
    bool vertical;
    if (ship < 0) return err(player, "unknown ship " + w[1]);
    if (!parseCell(w[2], row, col)) return err(player, "bad cell " + w[2]);
    if (!parseOrientation(w[3], vertical)) return err(player, "orientation must be H or V");

    Board& b = p_[player].board;
    PlaceError e = b.place(ship, row, col, vertical);
    if (e != PlaceError::None) return err(player, placeErrorText(e));

    send(player, std::string("PLACE_OK ") + SHIPS[ship].name);
    log(who(player) + " placed " + SHIPS[ship].name + " at " + cellName(row, col) + (vertical ? " V" : " H"));

    if (!b.allPlaced()) return;
    log(who(player) + " fleet ready");
    //PLACE_OK clears the client's waiting flag, so say WAIT again if the opponent isn't done
    if (!(joined(1 - player) && p_[1 - player].board.allPlaced())) send(player, "WAIT");
    maybeStart();
}

void Game::maybeStart() {
    if (phase_ != Phase::Placing) return;
    for (const Player& p : p_)
        if (!p.joined || !p.board.allPlaced()) return;

    setPhase(Phase::Playing);
    send(0, "START");
    send(1, "START");
    logBoards("start");
    turn_ = 0;   //player 1 fires first
    sendTurns();
}

void Game::sendTurns() {
    send(turn_, "YOUR_TURN");
    send(1 - turn_, "OPP_TURN");
    turnStart_ = now_;
}

void Game::logBoards(const char* when) {
    for (int i = 0; i < 2; ++i) log(who(i) + " board at " + when + ":\n" + p_[i].board.render());
}

// FIRE

void Game::handleFire(int player, const std::vector<std::string>& w) {
    if (phase_ != Phase::Playing) return err(player, "game has not started");
    if (player != turn_) return err(player, "not your turn");
    if (w.size() != 2) return err(player, "usage: FIRE <cell>");

    int row, col;
    if (!parseCell(w[1], row, col)) return err(player, "bad cell " + w[1]);

    int target = 1 - player;
    ShotResult res = p_[target].board.fire(row, col);
    if (!res.ok()) return err(player, shotErrorText(res.shot));   //turn stays with the shooter

    std::string cell = cellName(row, col);
    std::string token = shotToken(res);
    send(player, "RESULT " + cell + " " + token);
    send(target, "OPP_FIRED " + cell + " " + token);
    log(who(player) + " fired at " + cell + ": " + token);

    if (p_[target].board.allSunk()) return endGame(player, "all ships sunk");
    turn_ = target;   //strict alternation, hit or miss
    sendTurns();
}

// Game Over

void Game::endGame(int winner, const std::string& why) {
    int loser = 1 - winner;
    send(winner, "GAME_OVER WIN");
    send(loser, "GAME_OVER LOSE");
    setPhase(Phase::GameOver);
    log(who(winner) + " wins (" + why + ")");
    logBoards("game over");
    for (int i = 0; i < 2; ++i)
        if (connected(i)) kicks_.push_back(i);
    reset();
}

//forget both players and go back to waiting, the queued messages and kicks stay queued
void Game::reset() {
    p_[0] = Player{};
    p_[1] = Player{};
    turn_ = -1;
    setPhase(Phase::WaitingForPlayers);
}
