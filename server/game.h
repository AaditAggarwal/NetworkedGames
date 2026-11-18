//server game flow (role 2): state machine, turn order, move validation, message handling
//no sockets here. the server loop feeds in connects, lines and disconnects, then drains
//takeOutgoing() (send these), takeKicks() (close these sockets) and takeLog() (print these)
//
//players are slots 0 and 1, the protocol calls them player 1 and player 2
//states: WAITING_FOR_PLAYERS -> PLACING -> PLAYING -> GAME_OVER -> back to WAITING_FOR_PLAYERS
#pragma once

#include <string>
#include <vector>

#include "board.h"

enum class Phase { WaitingForPlayers, Placing, Playing, GameOver };
const char* phaseName(Phase p);   //"WAITING_FOR_PLAYERS" etc, for the log

struct OutMsg {
    int player;         //0 or 1
    std::string text;   //one protocol line without the trailing \n
};

class Game {
public:
    //turnTimeoutSec > 0 makes a player who stalls that long on their turn lose, 0 turns it off
    explicit Game(double turnTimeoutSec = 0);

    //---- called by the server loop ----
    void onConnect(int player, const std::string& addr);   //addr is "ip:port", for the log
    void onLine(int player, const std::string& line);      //one line from that player's socket
    void onDisconnect(int player);                          //recv() returned 0 or failed
    void tick(double now);                                  //call every loop pass, seconds from any steady clock

    //---- drained by the server loop after every call above ----
    //send everything in takeOutgoing() first, then close the sockets in takeKicks().
    //a kicked player is already forgotten, so don't call onDisconnect() for them
    std::vector<OutMsg> takeOutgoing();
    std::vector<int> takeKicks();
    std::vector<std::string> takeLog();

    //---- inspection, for the server and the tests ----
    Phase phase() const { return phase_; }
    int turn() const { return turn_; }   //slot whose turn it is, -1 outside PLAYING
    int freeSlot() const;                //first slot nobody is connected on, -1 if full
    bool connected(int player) const { return valid(player) && p_[player].connected; }
    bool joined(int player) const { return valid(player) && p_[player].joined; }   //sent HELLO
    const std::string& name(int player) const { return p_[valid(player) ? player : 0].name; }
    const Board& board(int player) const { return p_[valid(player) ? player : 0].board; }

    static constexpr size_t MAX_NAME = 16;

private:
    struct Player {
        bool connected = false;
        bool joined = false;
        std::string addr, name;
        Board board;
    };

    static bool valid(int player) { return player == 0 || player == 1; }
    std::string who(int player) const;   //"P1 Ann (1.2.3.4:5555)" for the log

    void send(int player, const std::string& text);
    void err(int player, const std::string& reason);
    void log(const std::string& text);
    void setPhase(Phase next);

    void handleHello(int player, const std::vector<std::string>& w);
    void handlePlace(int player, const std::vector<std::string>& w);
    void handleFire(int player, const std::vector<std::string>& w);

    void maybeStart();
    void sendTurns();
    void logBoards(const char* when);
    void endGame(int winner, const std::string& why);
    void reset();

    Player p_[2];
    Phase phase_ = Phase::WaitingForPlayers;
    int turn_ = -1;
    double timeout_;
    double now_ = 0;
    double turnStart_ = 0;
    std::vector<OutMsg> out_;
    std::vector<int> kicks_;
    std::vector<std::string> log_;
};
