# Battleship game logic (role 3)

Pure rules, no sockets: placing ships, firing, hit/miss/sunk, win detection. The server game flow (role 2) keeps one `Board` per player and turns the results into protocol lines.

Run the unit tests:

```
make test
```

## Files

| File | Role |
|---|---|
| `common/rules.h/.cpp` | Ship table, `parseCell` / `cellName`, `shipIndex`, `parseOrientation`. Same conventions as `client/state.cpp`, so the client can include this instead of its own copy |
| `server/board.h/.cpp` | `Board`: `place()`, `fire()`, `allPlaced()`, `allSunk()`, plus `render()` for the server log |
| `tests/test_board.cpp` | 26 unit tests, no framework needed |

## Conventions (same as client/README.md)

* `A1` is the top-left cell. The letter is the row (`A` to `J`) and the number is the column (`1` to `10`).
* `PLACE <ship> <cell> H|V`: the cell is the top-left end. `H` extends right and `V` extends down.
* Ships may touch but may not overlap.
* Firing at a cell already fired at, or off the board, is rejected. The board does not change, so the server replies `ERR` and the same player keeps the turn.
* A ship name follows the result only on `SUNK` (`SUNK CARRIER`).

## Using it from the server (role 2)

```cpp
// PLACE CARRIER A1 H
int ship = shipIndex(w[1]); int r, c; bool vert;
if (ship < 0 || !parseCell(w[2], r, c) || !parseOrientation(w[3], vert)) -> "ERR bad PLACE syntax"
PlaceError e = boards[p].place(ship, r, c, vert);
e == PlaceError::None ? "PLACE_OK " + w[1] : std::string("ERR ") + placeErrorText(e)
// once boards[0].allPlaced() && boards[1].allPlaced() -> START, YOUR_TURN / OPP_TURN

// FIRE B7 from player p, aimed at boards[1 - p]
ShotResult res = boards[1 - p].fire(r, c);
if (!res.ok())  -> "ERR " + shotErrorText(res.shot)   (turn does not change)
shooter: "RESULT "    + cellName(r, c) + " " + shotToken(res)
owner:   "OPP_FIRED " + cellName(r, c) + " " + shotToken(res)
if (boards[1 - p].allSunk()) -> GAME_OVER WIN / GAME_OVER LOSE, else swap turns
```
