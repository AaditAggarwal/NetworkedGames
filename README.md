# Networked Battleship

A two-player Battleship game you play over a network. Each player has their own window, and the two computers talk to each other over TCP.

## How it works

There is one program, `battleship`. When you open it, you pick **HOST** or **JOIN**.

- **HOST** starts a game on your computer and waits for someone to join.
- **JOIN** connects to a computer that is hosting.

The host's program also runs the game server in the background. The server knows where both players' ships are, checks every move and decides every hit and miss. Each player's window only shows their own ships, plus the hits and misses on the enemy.

## Playing a game

1. Both players connect to the same network. A phone hotspot works best, because school Wi-Fi often blocks computers from talking to each other.
2. **Host:** open `battleship`, type your name and click **HOST**. The window shows your address, like `192.168.1.23:5000`. Send that to the other player. If Windows asks about the firewall, click **Allow**.
3. **Other player:** open `battleship`, type the host's IP (like `192.168.1.23`), port `5000` and your name, then click **JOIN**.
4. Both windows switch to the game screen. Place your ships on **YOUR FLEET** (left grid):
   - move the mouse over the grid to see where the ship will go
   - press **R** or right-click to turn it
   - left-click to place it
   - ships are placed one at a time: Carrier (5), Battleship (4), Cruiser (3), Submarine (3), Destroyer (2)
5. When both players are done, the battle starts. The host shoots first.
6. On your turn, click a square on **ENEMY WATERS** (right grid). You see if it was a hit or a miss, and you get told when you sink a ship. Turns switch after every shot.
7. Sink all five enemy ships to win. Press **N** to go back to the start screen or **Esc** to quit.

If the other player closes their window, you see **OPPONENT LEFT**.

**Playing alone to test:** open `battleship` twice on one computer. Click HOST in the first window. In the second, type `127.0.0.1` and click JOIN.

**From a terminal** instead of the buttons:

```
battleship host 5000 Alice
battleship join 192.168.1.23 5000 Bob
```

### Controls

| When | What to do |
|---|---|
| Start screen | Tab or arrow keys to switch boxes. **JOIN** (or Enter) to join, **HOST** (or F2) to host |
| Placing ships | Mouse over YOUR FLEET to preview, **R** or right-click to rotate, left-click to place |
| Your turn | Left-click a square in ENEMY WATERS to fire |
| Game over | **N** for a new game, **Esc** to quit |
| Any time | **F1** turns on RAW MODE, which skips the window's own checks so you can see the server reject bad moves |

The bottom of the window shows every message sent (`>`) and received (`<`), so you can see the network traffic while you play.

## Sharing the game

On Windows the build makes one file, `build\battleship.exe`, with nothing else needed. Send it to the other player and they can run it straight away. If Windows shows "Windows protected your PC", click **More info → Run anyway**.

Mac and Linux players need to build it themselves (see below).

## Building

You need Python 3 and a C++ compiler:

- **Windows:** MinGW-w64 (g++)
- **Mac:** run `xcode-select --install`
- **Linux:** run `sudo apt install g++`

Then, in the project folder:

```
python -m venv .venv                 # Mac/Linux: python3
.venv\Scripts\activate               # Mac/Linux: source .venv/bin/activate
pip install -r requirements.txt
cmake -S . -B build -G Ninja -DFETCH_RAYLIB=ON
cmake --build build
```

- `pip` installs the build tools (CMake and Ninja) into the `.venv` folder only.
- CMake downloads raylib into the `build` folder by itself.
- So nothing gets installed on your computer for good.

On Linux, raylib also needs:

```
sudo apt install libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

This gives you, in `build/`:

| Program | What it does |
|---|---|
| `battleship` | The game |
| `battleship_server` | A server with no window, if you want one computer to only run the game. Usage: `battleship_server [port] [turn_timeout_seconds]`. It prints a timestamped log of everything that happens |
| `test_board`, `test_game`, `test_net` | Tests |

## Tests

```
build/test_board    # placing ships, firing, hit/miss/sunk
build/test_game     # turns, rules, wrong moves, disconnects, timeouts
build/test_net      # real connections on this computer, including a full game through the server
```

## The messages

The two sides send plain text, one message per line. You can even play by typing into `nc <host-ip> 5000`.

What the player sends:

| Message | Example | Meaning |
|---|---|---|
| `HELLO <name>` | `HELLO Alice` | First message after connecting |
| `PLACE <ship> <square> H/V` | `PLACE CARRIER A1 H` | Place a ship going right (H) or down (V) from that square |
| `FIRE <square>` | `FIRE B7` | Shoot at a square |

What the server sends back:

| Message | Meaning |
|---|---|
| `WELCOME 1` or `WELCOME 2` | You are player 1 or 2 |
| `WAIT` | Waiting for the other player |
| `PLACE_OK <ship>` | Your ship was placed |
| `START` | Both fleets are ready, the battle begins |
| `YOUR_TURN` / `OPP_TURN` | Whose turn it is |
| `RESULT B7 HIT` | What your shot did: `MISS`, `HIT` or `SUNK <ship>` |
| `OPP_FIRED B7 MISS` | Where the other player shot at you, and what it did |
| `GAME_OVER WIN` / `GAME_OVER LOSE` | The game is over |
| `OPP_LEFT` | The other player left |
| `ERR <reason>` | That move wasn't allowed. Nothing changes and it's still your turn |

Board rules:

- Squares go from `A1` (top-left) to `J10`. The letter is the row, the number is the column.
- Ships can touch but can't overlap or go off the board.
- You can't shoot the same square twice.

## The networking

All the networking is written by hand on the basic socket functions that come with the operating system (Winsock on Windows, sockets on Mac/Linux). No networking library is used.

- **Connecting:** the host listens on a port, the joiner connects, and TCP does its three-way handshake.
- **Nothing freezes:** connecting, sending and receiving never block, so the window keeps drawing while it waits. Joining gives up after 5 seconds if nobody answers.
- **Splitting messages:** TCP sends a stream of bytes, not separate messages. The program collects the bytes and splits them on the newline, so a message cut in half, or two messages arriving together, still work.
- **Fast replies:** Nagle's algorithm is turned off, so each short message goes out right away.
- **Leaving:** closing the window ends the connection. The other side notices (receiving 0 bytes) and shows that the opponent left.

## Files

| Path | What's in it |
|---|---|
| `common/net.h/.cpp` | The socket code: listen, connect, send, receive, split messages, find this computer's IP |
| `common/rules.h/.cpp` | Ship sizes and names, reading squares like `B7` |
| `server/board.h/.cpp` | One player's board: placing ships, taking shots, hit/miss/sunk |
| `server/game.h/.cpp` | The game itself: turns, checking moves, which messages to send |
| `server/game_server.h/.cpp` | Connects the game to the network: accepts two players, reads their messages, sends replies |
| `server/main.cpp` | The standalone `battleship_server` program |
| `client/main.cpp` | Starts the window, handles HOST / JOIN, runs the main loop |
| `client/conn.h/.cpp` | The player's connection to the server |
| `client/state.h/.cpp` | What the player knows: their ships and what they've learned about the enemy |
| `client/view.h/.cpp` | Draws the screens, both grids and the message log |
| `client/input.h/.cpp` | Mouse and keyboard |
| `tests/` | The tests |

## Problems

| You see | Try this |
|---|---|
| "connection refused" | Nobody is hosting at that IP and port. Check both |
| "no answer from the host" | Wrong IP, a firewall, or not on the same network. Try a phone hotspot |
| "port already in use" | Something else is using that port. Use a different port, like 5001 |
| The host shows more than one address | Use the one that starts with `192.168.` or `10.` on the network you're both on |
