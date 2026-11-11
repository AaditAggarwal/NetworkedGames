// main.cpp - raylib frame loop for the Battleship client.
//
// Usage: battleship_client [host] [port] [name]
//   With a host given, the client connects immediately; otherwise it opens
//   on the connect screen with these values pre-filled.
//
// Each frame: poll the socket (non-blocking), apply every complete server
// line to the state, handle input, draw. Nothing in the loop blocks.
#include <string>

#include "conn.h"
#include "input.h"
#include "raylib.h"
#include "state.h"
#include "view.h"

int main(int argc, char** argv) {
    ClientState s;
    if (argc > 1) s.host = argv[1];
    if (argc > 2) s.port = argv[2];
    if (argc > 3) s.name = argv[3];

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Battleship - CS 4390");
    SetTargetFPS(60);

    Conn conn;
    if (argc > 1) beginConnect(s, conn, GetTime());

    bool helloSent = false;
    while (!WindowShouldClose()) {
        double now = GetTime();

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
