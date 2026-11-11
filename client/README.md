# Battleship client GUI (role 4)

This folder contains the raylib window, both grids, the fog-of-war display, mouse and keyboard handling, and the status and log strips.

## Controls

| When | Action |
|---|---|
| Connect screen | Tab / arrow keys switch fields, Enter or the button connects |
| Placing | Hover over YOUR FLEET to preview, **R** or right-click to rotate, left-click to place |
| Your turn | Left-click a cell in ENEMY WATERS to fire |
| Game over | **N** to go back to the connect screen, **Esc** to quit |
| Any time | **F1** turns RAW MODE on or off, which skips the client-side checks so the server's `ERR` replies can be shown |

Command line: `battleship_client [host] [port] [name]`. Given a host, the client connects right away. Otherwise it opens the connect screen.

## Files

| File | Role |
|---|---|
| `main.cpp` | raylib frame loop: poll the connection, apply server lines, input, draw |
| `state.h/.cpp` | What the client knows (fog of war: only what RESULT / OPP_FIRED said). Parses each server line |
| `view.h/.cpp` | Draws the connect screen, both grids, legend, ship lists, status strip, protocol log |
| `input.h/.cpp` | Mouse and keys, which send `HELLO`, `PLACE` and `FIRE` |
| `conn.h` | **Interface only.** The network calls the GUI needs. Networking (role 1) implements it |

## What the GUI needs from other roles

* **Networking (role 1)**: an implementation of `conn.h`. It must provide a non-blocking connect, `poll()` called once per frame, line splitting, `send()` and `close()`.
* **Integration (role 5)**: a `battleship_client` target in CMake. It builds `client/*.cpp` plus the `conn.h` implementation and links `raylib` (and `ws2_32` on Windows).

## Protocol assumptions (please confirm with roles 1–3)

* **Cells**: the letter is the row (`A` to `J`, top to bottom) and the number is the column (`1` to `10`). `A1` is the top-left cell.
* **PLACE**: the cell is the ship's top-left end. `H` extends right and `V` extends down.
* **Ships**: `CARRIER 5`, `BATTLESHIP 4`, `CRUISER 3`, `SUBMARINE 3`, `DESTROYER 2`.
* `RESULT` and `OPP_FIRED` carry a ship name only when the result is `SUNK`.
* The client sends one `PLACE` at a time and waits for `PLACE_OK` or `ERR`. It sends one `FIRE` and waits for `RESULT` or `ERR`. Any `ERR` drops the pending move and shows the reason.
* Player 1 gets `WAIT`, but nothing tells them when Player 2 joins and placing starts. The GUI shows "Waiting for an opponent" and tolerates `ERR` if they try to place too early.
