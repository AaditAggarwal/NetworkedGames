// input.cpp - see input.h.
#include "input.h"

#include <cstdlib>

#include "raylib.h"
#include "view.h"

void sendMsg(ClientState& s, Conn& conn, const std::string& line) {
    if (conn.send(line)) s.addLog(true, line);
}

void beginConnect(ClientState& s, Conn& conn, double now) {
    int port = std::atoi(s.port.c_str());
    if (s.host.empty() || port <= 0 || port > 65535) {
        s.setNotice("Enter a host and a port between 1 and 65535", now, true);
        return;
    }
    if (s.name.empty()) s.name = "Player";
    for (char& c : s.name) if (c == ' ') c = '_';   // name is one protocol token
    s.resetSession();
    s.notice.clear();
    if (conn.start(s.host, port)) {
        s.phase = Phase::Connecting;
        s.peer = conn.peer();
    } else {
        s.phase = Phase::Connect;
        s.setNotice("Could not connect: " + conn.error(), now, true);
    }
}

static void connectScreenInput(ClientState& s, Conn& conn, double now) {
    const Layout& L = layout();
    std::string* fields[3] = {&s.host, &s.port, &s.name};
    const size_t maxLen[3] = {60, 5, 16};

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        for (int i = 0; i < 3; ++i)
            if (CheckCollisionPointRec(m, L.fields[i])) s.focus = i;
        if (CheckCollisionPointRec(m, L.connectBtn)) { beginConnect(s, conn, now); return; }
    }
    if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_DOWN))
        s.focus = (s.focus + (IsKeyDown(KEY_LEFT_SHIFT) ? 2 : 1)) % 3;
    if (IsKeyPressed(KEY_UP)) s.focus = (s.focus + 2) % 3;

    std::string& f = *fields[s.focus];
    for (int ch = GetCharPressed(); ch > 0; ch = GetCharPressed()) {
        if (ch < 32 || ch > 126 || f.size() >= maxLen[s.focus]) continue;
        if (s.focus == 1 && (ch < '0' || ch > '9')) continue;
        f.push_back((char)ch);
    }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !f.empty()) f.pop_back();
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) beginConnect(s, conn, now);
}

static void placingInput(ClientState& s, Conn& conn, double now) {
    if (IsKeyPressed(KEY_R) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) s.vertical = !s.vertical;

    int ship = s.nextShipToPlace();
    if (ship < 0 || s.pendingShip >= 0) return;   // one PLACE in flight at a time
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    int r, c;
    if (!cellAt(layout().own, GetMousePosition(), r, c)) return;
    if (!s.rawMode && !s.fits(ship, r, c, s.vertical)) {
        s.setNotice(std::string(SHIPS[ship].name) + " doesn't fit there", now, true);
        return;
    }
    s.pendingShip = ship;
    s.pendingPlace = {r, c, s.vertical};
    sendMsg(s, conn, std::string("PLACE ") + SHIPS[ship].name + " " + cellName(r, c) + " " +
                         (s.vertical ? "V" : "H"));
}

static void playingInput(ClientState& s, Conn& conn, double now) {
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    int r, c;
    if (!cellAt(layout().enemy, GetMousePosition(), r, c)) return;
    if (!s.rawMode) {
        if (!s.myTurn) { s.setNotice("Not your turn yet", now, true); return; }
        if (s.pendingRow >= 0) return;
        if (s.enemy[r][c] != Mark::None) { s.setNotice("You already fired at " + cellName(r, c), now, true); return; }
    }
    s.pendingRow = r;
    s.pendingCol = c;
    sendMsg(s, conn, "FIRE " + cellName(r, c));
}

void handleInput(ClientState& s, Conn& conn, double now) {
    if (IsKeyPressed(KEY_F1)) s.rawMode = !s.rawMode;

    switch (s.phase) {
    case Phase::Connect:    connectScreenInput(s, conn, now); break;
    case Phase::Connecting: break;
    case Phase::Placing:    placingInput(s, conn, now); break;
    case Phase::Playing:    playingInput(s, conn, now); break;
    case Phase::GameOver:
        if (IsKeyPressed(KEY_N)) {
            conn.close();
            s.resetSession();
        }
        break;
    }
}
