// view.h - drawing. Pure output: reads ClientState, never changes it.
#pragma once

#include "raylib.h"
#include "state.h"

constexpr int SCREEN_W = 1040;
constexpr int SCREEN_H = 760;
constexpr float CELL = 34.0f;

struct Layout {
    Rectangle own;     // YOUR FLEET grid
    Rectangle enemy;   // ENEMY WATERS grid
    Rectangle fields[3];   // connect screen: host, port, name
    Rectangle connectBtn;
};

const Layout& layout();

// Mouse position -> cell inside a grid. False if outside.
bool cellAt(const Rectangle& grid, Vector2 mouse, int& row, int& col);

void drawFrame(const ClientState& s, double now);
