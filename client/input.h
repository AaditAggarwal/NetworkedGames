// input.h - mouse/keyboard -> state changes and outgoing PLACE / FIRE / HELLO.
#pragma once

#include "conn.h"
#include "state.h"

// Send one protocol line and record it in the log strip.
void sendMsg(ClientState& s, Conn& conn, const std::string& line);

// Begin connecting with the host/port typed on the connect screen.
void beginConnect(ClientState& s, Conn& conn, double now);

// Handle this frame's input. Call once per frame.
void handleInput(ClientState& s, Conn& conn, double now);
