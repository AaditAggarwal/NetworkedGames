#include "game_server.h"

#include <chrono>

#include "game.h"

static double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static std::string tag(int p) { return "P" + std::to_string(p + 1); }

GameServer::GameServer(double turnTimeoutSec) : timeout_(turnTimeoutSec), game_(new Game(turnTimeoutSec)) {}

GameServer::~GameServer() { stop(); }

bool GameServer::start(uint16_t port) {
    stop();
    err_.clear();
    listen_ = listenOn(port, err_);
    if (listen_ == BAD_SOCK) return false;
    log("listening on port " + std::to_string(port));
    return true;
}

void GameServer::stop() {
    for (int p = 0; p < 2; ++p) closeSlot(p);
    closeSock(listen_);
    listen_ = BAD_SOCK;
    game_.reset(new Game(timeout_));
}

std::vector<std::string> GameServer::takeLog() {
    std::vector<std::string> out;
    out.swap(log_);
    return out;
}

void GameServer::step(int waitMs) {
    if (!running()) return;
    waitReadable({listen_, slots_[0].s, slots_[1].s}, waitMs);

    game_->tick(nowSec());
    flush();
    //clients first, a slot closed here may be reused by accept below
    for (int p = 0; p < 2; ++p)
        if (slots_[p].s != BAD_SOCK) readSlot(p);
    acceptNew();
}

void GameServer::acceptNew() {
    for (sock_t s; (s = acceptClient(listen_)) != BAD_SOCK;) {
        prepareSocket(s);
        std::string addr = peerAddress(s);
        int p = game_->freeSlot();
        if (p < 0 || slots_[p].s != BAD_SOCK) {
            log("rejecting " + addr + ", server full");
            sendLine(s, "ERR server full");
            closeSock(s);
            continue;
        }
        slots_[p] = Slot{};
        slots_[p].s = s;
        slots_[p].addr = addr;
        log("accepted " + addr + " as " + tag(p));
        game_->onConnect(p, addr);
        flush();
    }
}

void GameServer::readSlot(int p) {
    sock_t s = slots_[p].s;
    bool open = slots_[p].r.pump(s);
    std::string line;
    //lines that arrived before a FIN still count
    while (slots_[p].s == s && slots_[p].r.next(line)) {
        log("< " + tag(p) + " " + line);
        game_->onLine(p, line);
        flush();   //may kick this slot (game over), the loop condition stops then
    }
    if (!open && slots_[p].s == s) {
        log(tag(p) + " " + slots_[p].addr + " closed the connection");
        closeSlot(p);
        game_->onDisconnect(p);
        flush();
    }
}

//order matters: GAME_OVER and OPP_LEFT must go out before the FIN
void GameServer::flush() {
    for (const std::string& l : game_->takeLog()) log("  * " + l);
    for (const OutMsg& m : game_->takeOutgoing()) {
        if (slots_[m.player].s == BAD_SOCK) continue;
        log("> " + tag(m.player) + " " + m.text);
        if (!sendLine(slots_[m.player].s, m.text)) log("  ! send to " + tag(m.player) + " failed");
    }
    for (int p : game_->takeKicks()) {
        if (slots_[p].s == BAD_SOCK) continue;
        log("closing " + tag(p) + " " + slots_[p].addr);
        closeSlot(p);
    }
}

void GameServer::closeSlot(int p) {
    closeSock(slots_[p].s);   //sends FIN
    slots_[p] = Slot{};
}
