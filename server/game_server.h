//runs Game over real sockets: listener, two client slots, line framing
//used by server/main.cpp and by the client when it hosts a game
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../common/net.h"

class Game;   //not included here, its Phase clashes with the client's

class GameServer {
public:
    explicit GameServer(double turnTimeoutSec = 0);
    ~GameServer();
    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

    bool start(uint16_t port);   //false + error() if the port can't be opened
    void step(int waitMs);       //accept, read, run Game, send. waits at most waitMs for traffic
    void stop();

    bool running() const { return listen_ != BAD_SOCK; }
    const std::string& error() const { return err_; }
    std::vector<std::string> takeLog();

private:
    struct Slot {
        sock_t s = BAD_SOCK;
        std::string addr;
        LineReader r;
    };

    void acceptNew();
    void readSlot(int p);
    void flush();
    void closeSlot(int p);
    void log(const std::string& text) { log_.push_back(text); }

    double timeout_;
    std::unique_ptr<Game> game_;
    sock_t listen_ = BAD_SOCK;
    Slot slots_[2];
    std::string err_;
    std::vector<std::string> log_;
};
