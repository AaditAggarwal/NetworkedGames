// view.cpp - draws the connect screen, both grids, status strip and log strip.
#include "view.h"

#include <cmath>
#include <string>

// ------------------------------------------------------------------ palette
static const Color BG        = {16, 24, 40, 255};
static const Color PANEL     = {24, 36, 58, 255};
static const Color PANEL_HI  = {34, 50, 80, 255};
static const Color WATER     = {28, 84, 150, 255};
static const Color WATER_HOV = {48, 112, 184, 255};
static const Color GRIDLINE  = {14, 46, 86, 255};
static const Color SHIP      = {150, 156, 168, 255};
static const Color SHIP_EDGE = {96, 102, 114, 255};
static const Color SHIP_SUNK = {92, 60, 64, 255};
static const Color HIT       = {226, 54, 54, 255};
static const Color SUNK      = {120, 22, 26, 255};
static const Color MISS      = {236, 240, 246, 255};
static const Color TEXT      = {226, 232, 240, 255};
static const Color DIM       = {136, 150, 172, 255};
static const Color ACCENT    = {250, 204, 21, 255};
static const Color GOOD      = {74, 222, 128, 255};
static const Color BAD       = {248, 113, 113, 255};
static const Color OUT_TXT   = {134, 239, 172, 255};
static const Color IN_TXT    = {253, 224, 71, 255};

static const float GRID_PX = CELL * GRID;
static const float GRID_TOP = 160.0f;

const Layout& layout() {
    static Layout L = [] {
        Layout l;
        l.own   = {90.0f, GRID_TOP, GRID_PX, GRID_PX};
        l.enemy = {SCREEN_W - 90.0f - GRID_PX, GRID_TOP, GRID_PX, GRID_PX};
        for (int i = 0; i < 3; ++i)
            l.fields[i] = {SCREEN_W / 2.0f - 170.0f, 250.0f + i * 80.0f, 340.0f, 40.0f};
        l.connectBtn = {SCREEN_W / 2.0f - 90.0f, 500.0f, 180.0f, 46.0f};
        return l;
    }();
    return L;
}

bool cellAt(const Rectangle& g, Vector2 m, int& row, int& col) {
    if (!CheckCollisionPointRec(m, g)) return false;
    col = (int)((m.x - g.x) / CELL);
    row = (int)((m.y - g.y) / CELL);
    if (row < 0 || row >= GRID || col < 0 || col >= GRID) return false;
    return true;
}

static Rectangle cellRect(const Rectangle& g, int row, int col) {
    return {g.x + col * CELL, g.y + row * CELL, CELL, CELL};
}

static Vector2 cellCenter(const Rectangle& g, int row, int col) {
    return {g.x + col * CELL + CELL / 2, g.y + row * CELL + CELL / 2};
}

static void textCentered(const char* t, float cx, float y, int size, Color c) {
    DrawText(t, (int)(cx - MeasureText(t, size) / 2.0f), (int)y, size, c);
}

static void drawX(const Rectangle& r, float pad, float thick, Color c) {
    DrawLineEx({r.x + pad, r.y + pad}, {r.x + r.width - pad, r.y + r.height - pad}, thick, c);
    DrawLineEx({r.x + r.width - pad, r.y + pad}, {r.x + pad, r.y + r.height - pad}, thick, c);
}

// Water, labels A..J / 1..10, grid lines, title.
static void drawGridBase(const Rectangle& g, const char* title, bool active) {
    textCentered(title, g.x + g.width / 2, g.y - 52, 22, active ? ACCENT : TEXT);
    for (int c = 0; c < GRID; ++c) {
        std::string n = std::to_string(c + 1);
        textCentered(n.c_str(), g.x + c * CELL + CELL / 2, g.y - 22, 16, DIM);
    }
    for (int r = 0; r < GRID; ++r) {
        char l[2] = {(char)('A' + r), 0};
        DrawText(l, (int)(g.x - 22), (int)(g.y + r * CELL + 9), 16, DIM);
    }
    DrawRectangleRec(g, WATER);
    for (int i = 0; i <= GRID; ++i) {
        DrawLineEx({g.x + i * CELL, g.y}, {g.x + i * CELL, g.y + g.height}, 1.0f, GRIDLINE);
        DrawLineEx({g.x, g.y + i * CELL}, {g.x + g.width, g.y + i * CELL}, 1.0f, GRIDLINE);
    }
    DrawRectangleLinesEx({g.x - 2, g.y - 2, g.width + 4, g.height + 4}, 2.0f,
                         active ? ACCENT : PANEL_HI);
}

static Rectangle shipRect(const Rectangle& g, const Placement& p, int len) {
    Rectangle a = cellRect(g, p.row, p.col);
    float w = p.vertical ? CELL : CELL * len;
    float h = p.vertical ? CELL * len : CELL;
    return {a.x + 3, a.y + 3, w - 6, h - 6};
}

static void drawShip(const Rectangle& g, const Placement& p, int len, Color fill, Color edge) {
    Rectangle r = shipRect(g, p, len);
    DrawRectangleRounded(r, 0.45f, 6, fill);
    DrawRectangleRoundedLinesEx(r, 0.45f, 6, 2.0f, edge);
}

// ------------------------------------------------------------------ grids

static void drawOwnGrid(const ClientState& s, double now) {
    const Rectangle& g = layout().own;
    bool placing = s.phase == Phase::Placing;
    drawGridBase(g, "YOUR FLEET", placing && !s.allPlaced());

    for (int i = 0; i < NUM_SHIPS; ++i)
        if (s.placed[i])
            drawShip(g, s.fleet[i], SHIPS[i].len, s.sunk[i] ? SHIP_SUNK : SHIP, SHIP_EDGE);

    if (s.pendingShip >= 0) {   // sent PLACE, waiting for PLACE_OK
        float a = 0.45f + 0.25f * (float)std::sin(now * 8.0);
        drawShip(g, s.pendingPlace, SHIPS[s.pendingShip].len, Fade(SHIP, a), Fade(SHIP_EDGE, a));
    }

    // Ghost ship under the mouse while placing.
    int next = s.nextShipToPlace();
    int r, c;
    if (placing && next >= 0 && s.pendingShip < 0 && cellAt(g, GetMousePosition(), r, c)) {
        bool ok = s.fits(next, r, c, s.vertical);
        Color col = ok ? GOOD : BAD;
        int len = SHIPS[next].len;
        for (int k = 0; k < len; ++k) {
            int rr = r + (s.vertical ? k : 0), cc = c + (s.vertical ? 0 : k);
            if (rr >= GRID || cc >= GRID) break;
            Rectangle cr = cellRect(g, rr, cc);
            DrawRectangleRec({cr.x + 2, cr.y + 2, cr.width - 4, cr.height - 4}, Fade(col, 0.55f));
        }
    }

    for (int rr = 0; rr < GRID; ++rr)
        for (int cc = 0; cc < GRID; ++cc) {
            Rectangle cr = cellRect(g, rr, cc);
            if (s.onMe[rr][cc] == Mark::Hit || s.onMe[rr][cc] == Mark::Sunk) {
                drawX(cr, 7, 4.0f, HIT);
            } else if (s.onMe[rr][cc] == Mark::Miss) {
                DrawCircleV(cellCenter(g, rr, cc), 4.0f, MISS);
            }
        }
}

static void drawEnemyGrid(const ClientState& s, double now) {
    const Rectangle& g = layout().enemy;
    bool firing = s.phase == Phase::Playing && s.myTurn && s.pendingRow < 0;
    drawGridBase(g, "ENEMY WATERS", firing);

    int hr = -1, hc = -1;
    bool hovering = s.phase == Phase::Playing && cellAt(g, GetMousePosition(), hr, hc);

    for (int r = 0; r < GRID; ++r)
        for (int c = 0; c < GRID; ++c) {
            Rectangle cr = cellRect(g, r, c);
            Rectangle in = {cr.x + 1, cr.y + 1, cr.width - 2, cr.height - 2};
            switch (s.enemy[r][c]) {
            case Mark::None:
                if (hovering && firing && r == hr && c == hc) DrawRectangleRec(in, WATER_HOV);
                break;
            case Mark::Miss:
                DrawCircleV(cellCenter(g, r, c), 8.0f, MISS);
                DrawCircleLinesV(cellCenter(g, r, c), 8.0f, DIM);
                break;
            case Mark::Hit:
                DrawRectangleRec(in, HIT);
                DrawCircleV(cellCenter(g, r, c), 6.0f, {255, 170, 120, 255});
                break;
            case Mark::Sunk:
                DrawRectangleRec(in, SUNK);
                drawX(cr, 8, 3.0f, HIT);
                break;
            }
        }

    if (s.pendingRow >= 0) {   // FIRE sent, waiting for RESULT
        float a = 0.5f + 0.5f * (float)std::sin(now * 10.0);
        DrawRectangleLinesEx(cellRect(g, s.pendingRow, s.pendingCol), 3.0f, Fade(ACCENT, a));
    }

    // Crosshair on the hovered cell.
    if (hovering && firing) {
        Rectangle cr = cellRect(g, hr, hc);
        bool fresh = s.enemy[hr][hc] == Mark::None || s.rawMode;
        Color col = fresh ? ACCENT : BAD;
        DrawRectangleLinesEx(cr, 3.0f, col);
        Vector2 m = cellCenter(g, hr, hc);
        DrawLineEx({m.x - 9, m.y}, {m.x + 9, m.y}, 2.0f, col);
        DrawLineEx({m.x, m.y - 9}, {m.x, m.y + 9}, 2.0f, col);
    }
}

// Ship roster under each grid.
static void drawRoster(const Rectangle& g, const bool* doneFlags, const bool* sunkFlags,
                       const char* caption, int highlight) {
    float y = g.y + g.height + 16;
    DrawText(caption, (int)g.x, (int)y, 16, DIM);
    y += 22;
    float x = g.x;
    for (int i = 0; i < NUM_SHIPS; ++i) {
        const ShipType& t = SHIPS[i];
        bool isSunk = sunkFlags[i];
        bool done = doneFlags ? doneFlags[i] : true;
        Color fill = isSunk ? SUNK : (done ? SHIP : PANEL_HI);
        if (i == highlight) fill = ACCENT;
        float w = 9.0f * t.len;
        // small pip-bar for the ship length
        for (int k = 0; k < t.len; ++k)
            DrawRectangle((int)(x + k * 9), (int)y, 7, 10, fill);
        const char* label = t.name;
        DrawText(label, (int)x, (int)(y + 14), 10, isSunk ? BAD : (i == highlight ? ACCENT : TEXT));
        float lw = (float)MeasureText(label, 10);
        x += (w > lw ? w : lw) + 14;
    }
}

static void drawLegend() {
    const Layout& L = layout();
    float x = L.own.x + L.own.width + 34;
    float y = GRID_TOP + 20;
    DrawText("LEGEND", (int)x, (int)y, 16, DIM);
    y += 28;
    auto row = [&](auto drawSwatch, const char* label) {
        Rectangle sw = {x, y, 22, 22};
        DrawRectangleRec(sw, WATER);
        drawSwatch(sw);
        DrawText(label, (int)(x + 30), (int)(y + 4), 14, TEXT);
        y += 32;
    };
    row([](Rectangle r) { DrawRectangleRounded({r.x + 2, r.y + 5, r.width - 4, r.height - 10}, 0.5f, 4, SHIP); }, "your ship");
    row([](Rectangle r) { drawX(r, 4, 3.0f, HIT); }, "hit on you");
    row([](Rectangle r) { DrawCircleV({r.x + 11, r.y + 11}, 3.0f, MISS); }, "their miss");
    row([](Rectangle) {}, "unknown");
    row([](Rectangle r) { DrawRectangleRec(r, HIT); }, "your hit");
    row([](Rectangle r) { DrawCircleV({r.x + 11, r.y + 11}, 6.0f, MISS); }, "your miss");
    row([](Rectangle r) { DrawRectangleRec(r, SUNK); drawX(r, 5, 2.0f, HIT); }, "sunk");
}

// ------------------------------------------------------------------ strips

static std::string phaseText(const ClientState& s) {
    switch (s.phase) {
    case Phase::Connect:    return "NOT CONNECTED";
    case Phase::Connecting: return "CONNECTING";
    case Phase::Placing:    return s.allPlaced() ? "FLEET READY" : "PLACING SHIPS";
    case Phase::Playing:    return !s.turnKnown ? "STARTING" : (s.myTurn ? "YOUR TURN" : "OPPONENT'S TURN");
    case Phase::GameOver:   return s.oppLeft ? "OPPONENT LEFT" : (s.connLost ? "DISCONNECTED" : (s.won ? "YOU WON" : "YOU LOST"));
    }
    return "";
}

static void drawBanner(const ClientState& s, double now) {
    std::string msg;
    Color col = TEXT;
    int next = s.nextShipToPlace();
    if (s.phase == Phase::Placing) {
        if (s.pendingShip >= 0) {
            msg = std::string("Placing ") + SHIPS[s.pendingShip].name + "...";
        } else if (next >= 0) {
            msg = std::string("Place your ") + SHIPS[next].name + " (" + std::to_string(SHIPS[next].len) +
                  ")  -  click to place, R / right-click to rotate [" + (s.vertical ? "VERTICAL" : "HORIZONTAL") + "]";
            col = ACCENT;
        } else {
            msg = "Fleet deployed. Waiting for the opponent to finish placing...";
        }
        if (s.waiting && !s.allPlaced()) msg = "Waiting for an opponent to connect...   " + msg;
    } else if (s.phase == Phase::Playing) {
        if (!s.turnKnown) msg = "Battle stations!";
        else if (s.myTurn) { msg = s.pendingRow >= 0 ? "Firing..." : "YOUR TURN  -  click a cell in ENEMY WATERS to fire"; col = ACCENT; }
        else msg = "Opponent is aiming...";
    }

    // Recent notices (errors, sinkings) override for a few seconds.
    if (now - s.noticeTime < 3.5 && !s.notice.empty()) {
        msg = s.notice;
        col = s.noticeError ? BAD : GOOD;
    }
    DrawRectangle(0, 66, SCREEN_W, 34, PANEL);
    textCentered(msg.c_str(), SCREEN_W / 2.0f, 74, 18, col);
}

static void drawHeader(const ClientState& s) {
    DrawRectangle(0, 0, SCREEN_W, 58, PANEL);
    DrawText("BATTLESHIP", 24, 14, 30, TEXT);
    std::string right = s.name;
    if (s.playerNum) right += "  -  Player " + std::to_string(s.playerNum);
    DrawText(right.c_str(), SCREEN_W - 24 - MeasureText(right.c_str(), 20), 20, 20, DIM);
    if (s.phase == Phase::Playing || s.phase == Phase::GameOver) {
        std::string score = "Shots " + std::to_string(s.shotsFired) + "   Hits " + std::to_string(s.shotsHit) +
                            "   Enemy ships left " + std::to_string(s.enemyShipsLeft()) +
                            "   Yours " + std::to_string(s.myShipsLeft());
        textCentered(score.c_str(), SCREEN_W / 2.0f, 22, 16, DIM);
    }
}

static void drawStatusAndLog(const ClientState& s) {
    float y = 596;
    DrawRectangle(0, (int)y, SCREEN_W, 30, PANEL_HI);
    std::string conn = s.phase == Phase::Connect ? "Not connected"
                     : (s.connLost ? "Disconnected " : (s.phase == Phase::Connecting ? "Connecting " : "Connected ")) + s.peer;
    std::string status = conn + "  |  " + (s.playerNum ? "Player " + std::to_string(s.playerNum) : "Player ?") +
                         "  |  " + phaseText(s);
    DrawText(status.c_str(), 16, (int)y + 7, 18, TEXT);
    if (s.rawMode) {
        const char* raw = "RAW MODE (F1): client checks off";
        DrawText(raw, SCREEN_W - 16 - MeasureText(raw, 16), (int)y + 8, 16, BAD);
    }

    // Protocol log: newest at the bottom.
    float top = y + 34;
    DrawRectangle(0, (int)top - 4, SCREEN_W, SCREEN_H - (int)top + 4, {10, 16, 28, 255});
    const int lineH = 18;
    int rows = (int)((SCREEN_H - top - 4) / lineH);
    int start = (int)s.log.size() - rows;
    if (start < 0) start = 0;
    for (int i = start; i < (int)s.log.size(); ++i) {
        const LogLine& l = s.log[i];
        std::string t = (l.outgoing ? "> " : "< ") + l.text;
        Color c = l.outgoing ? OUT_TXT : (l.text.rfind("ERR", 0) == 0 ? BAD : IN_TXT);
        DrawText(t.c_str(), 16, (int)top + (i - start) * lineH, 16, c);
    }
    if (s.log.empty())
        DrawText("protocol messages appear here  (> sent   < received)", 16, (int)top, 16, DIM);
}

// ------------------------------------------------------------------ screens

static void drawConnectScreen(const ClientState& s, double now) {
    const Layout& L = layout();
    textCentered("BATTLESHIP", SCREEN_W / 2.0f, 110, 56, TEXT);
    textCentered("CS 4390 - Networked Battleship over TCP", SCREEN_W / 2.0f, 176, 18, DIM);

    const char* labels[3] = {"Server host / IP", "Port", "Your name"};
    const std::string* vals[3] = {&s.host, &s.port, &s.name};
    bool editable = s.phase == Phase::Connect;
    for (int i = 0; i < 3; ++i) {
        Rectangle f = L.fields[i];
        DrawText(labels[i], (int)f.x, (int)f.y - 22, 16, DIM);
        DrawRectangleRec(f, PANEL);
        bool focused = editable && s.focus == i;
        DrawRectangleLinesEx(f, 2.0f, focused ? ACCENT : PANEL_HI);
        std::string v = *vals[i];
        if (focused && std::fmod(now, 1.0) < 0.5) v += "_";
        DrawText(v.c_str(), (int)f.x + 10, (int)f.y + 10, 20, TEXT);
    }

    Rectangle b = L.connectBtn;
    bool hov = editable && CheckCollisionPointRec(GetMousePosition(), b);
    DrawRectangleRounded(b, 0.3f, 6, editable ? (hov ? GOOD : Color{34, 160, 90, 255}) : PANEL_HI);
    textCentered(editable ? "CONNECT" : "CONNECTING...", b.x + b.width / 2, b.y + 13, 20, BG);

    if (s.phase == Phase::Connecting) {
        std::string m = s.peer.empty() ? "Connecting..." : "TCP handshake with " + s.peer + " ... waiting for WELCOME";
        textCentered(m.c_str(), SCREEN_W / 2.0f, 566, 16, DIM);
    } else if (!s.notice.empty()) {
        textCentered(s.notice.c_str(), SCREEN_W / 2.0f, 566, 18, BAD);
    } else {
        textCentered("Tab to switch fields, Enter to connect", SCREEN_W / 2.0f, 566, 16, DIM);
    }
}

static void drawGameOver(const ClientState& s) {
    DrawRectangle(0, 0, SCREEN_W, 596, Fade(BLACK, 0.55f));
    Rectangle box = {SCREEN_W / 2.0f - 260, 200, 520, 190};
    DrawRectangleRounded(box, 0.12f, 8, PANEL);
    DrawRectangleRoundedLinesEx(box, 0.12f, 8, 2.0f, PANEL_HI);
    const char* big;
    Color col;
    if (s.oppLeft)       { big = "OPPONENT LEFT"; col = ACCENT; }
    else if (s.connLost) { big = "CONNECTION LOST"; col = BAD; }
    else if (s.won)      { big = "VICTORY!"; col = GOOD; }
    else                 { big = "DEFEAT"; col = BAD; }
    textCentered(big, SCREEN_W / 2.0f, 230, 64, col);
    std::string sub = "Shots " + std::to_string(s.shotsFired) + "  -  Hits " + std::to_string(s.shotsHit);
    textCentered(sub.c_str(), SCREEN_W / 2.0f, 310, 22, TEXT);
    textCentered("Press N for a new connection, ESC to quit", SCREEN_W / 2.0f, 350, 18, DIM);
}

void drawFrame(const ClientState& s, double now) {
    BeginDrawing();
    ClearBackground(BG);

    if (s.phase == Phase::Connect || s.phase == Phase::Connecting) {
        drawConnectScreen(s, now);
    } else {
        const Layout& L = layout();
        drawHeader(s);
        drawBanner(s, now);
        drawOwnGrid(s, now);
        drawEnemyGrid(s, now);
        drawLegend();
        int next = s.phase == Phase::Placing ? s.nextShipToPlace() : -1;
        drawRoster(L.own, s.placed, s.sunk, "Your fleet", next);
        drawRoster(L.enemy, nullptr, s.enemySunk, "Enemy fleet (sunk = red)", -1);
        if (s.phase == Phase::GameOver) drawGameOver(s);
    }
    drawStatusAndLog(s);
    EndDrawing();
}
