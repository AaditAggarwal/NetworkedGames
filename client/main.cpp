// main.cpp - raylib frame loop for the Battleship client.
//
// Usage: battleship host <port> [name]          host a game, the other player joins you
//        battleship join <ip> <port> [name]     join a host
//        battleship [ip] [port] [name]          same as join; no args opens the connect screen
//
// Hosting runs the game server (server/game_server) inside this window; our
// own client joins it over 127.0.0.1 and the other player over the network.
//
// Each frame: poll the socket (non-blocking), apply every complete server
// line to the state, handle input, draw. Nothing in the loop blocks.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../server/game_server.h"
#include "conn.h"
#include "input.h"
#include "raylib.h"
#include "state.h"
#include "view.h"

static void beginHost(ClientState& s, Conn& conn, GameServer& server, double now) {
    int port = std::atoi(s.port.c_str());
    if (port <= 0 || port > 65535) { s.setNotice("Enter a port between 1 and 65535", now, true); return; }
    if (!server.start((uint16_t)port)) { s.setNotice(server.error(), now, true); return; }

    std::string typed = s.host;
    s.host = "127.0.0.1";
    beginConnect(s, conn, now);
    s.host = typed;
    if (s.phase != Phase::Connecting) { server.stop(); return; }

    s.hosting = true;
    for (const std::string& ip : localAddresses()) s.joinAt += (s.joinAt.empty() ? "" : " or ") + ip + ":" + s.port;
    if (s.joinAt.empty()) s.joinAt = "this computer's IP, port " + s.port;
}

int main(int argc, char** argv) {
    ClientState s;
    std::string mode = argc > 1 ? argv[1] : "";
    int a = (mode == "host" || mode == "join") ? 2 : 1;
    if (mode == "host") {
        if (argc > 2) s.port = argv[2];
        if (argc > 3) s.name = argv[3];
        s.wantHost = true;
    } else {
        if (argc > a) s.host = argv[a];
        if (argc > a + 1) s.port = argv[a + 1];
        if (argc > a + 2) s.name = argv[a + 2];
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Battleship - CS 4390");
    SetTargetFPS(60);

    Conn conn;
    GameServer server;
    if (!s.wantHost && argc > a) beginConnect(s, conn, GetTime());

    bool helloSent = false;
    while (!WindowShouldClose()) {
        double now = GetTime();

        if (s.wantHost) {
            s.wantHost = false;
            beginHost(s, conn, server, now);
        }
        if (s.phase == Phase::Connect && server.running()) server.stop();
        server.step(0);
        for (const std::string& l : server.takeLog()) std::printf("[server] %s\n", l.c_str());
        std::fflush(stdout);

        if (s.phase != Phase::Connect) {
            Conn::Status st = conn.poll();

            if (st == Conn::Status::Connected && !helloSent) {
                // TCP handshake is done; now the app-level handshake.
                sendMsg(s, conn, "HELLO " + s.name);
                helloSent = true;
            }

            std::string line;
            while (conn.nextLine(line)) s.handleLine(line, now);

            if (st == Conn::Status::Failed || st == Conn::Status::Closed) {
                if (s.phase == Phase::Connecting) {
                    s.phase = Phase::Connect;
                    s.setNotice("Could not connect: " + conn.error(), now, true);
                } else if (s.phase != Phase::GameOver) {
                    s.connLost = true;
                    s.phase = Phase::GameOver;
                    s.myTurn = false;
                }
                helloSent = false;
            }
        } else {
            helloSent = false;
        }

        handleInput(s, conn, now);
        drawFrame(s, now);
    }

    conn.close();   // FIN to the server
    CloseWindow();
    return 0;
}
