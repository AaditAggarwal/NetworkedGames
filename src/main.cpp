//usage: battleship host <port> [name] | battleship join <ip> <port> [name] | battleship
#include <chrono>
#include <ctime>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "demo_fleet.h"
#include "net.h"
#include "peer_game.h"

using namespace p2p;

static std::mutex inMutex;
static std::deque<std::string> inQueue;

static void readStdin() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::lock_guard<std::mutex> lock(inMutex);
        inQueue.push_back(line);
    }
}

static bool popInput(std::string& out) {
    std::lock_guard<std::mutex> lock(inMutex);
    if (inQueue.empty()) return false;
    out = inQueue.front();
    inQueue.pop_front();
    return true;
}

static std::string ask(const std::string& prompt, const std::string& def) {
    std::cout << prompt << " [" << def << "]: " << std::flush;
    std::string s;
    std::getline(std::cin, s);
    return s.empty() ? def : s;
}

static std::string renderEnemy(const PeerGame& g) {
    std::string out = "   1 2 3 4 5 6 7 8 9 10\n";
    for (int r = 0; r < GRID; ++r) {
        out += (char)('A' + r);
        out += ' ';
        for (int c = 0; c < GRID; ++c) {
            Mark m = g.enemy(r, c);
            out += ' ';
            out += m == Mark::Hit ? 'X' : m == Mark::Sunk ? '#' : m == Mark::Miss ? 'o' : '.';
        }
        out += '\n';
    }
    return out;
}

static sock_t connectInteractive(bool& isHost, std::string& name) {
    isHost = ask("host or join? (h/j)", "h") != "j";
    std::string ip = isHost ? "" : ask("host IP", "127.0.0.1");
    int port = std::stoi(ask("port", "5000"));
    name = ask("your name", "Player");

    Connector c;
    if (isHost) {
        if (!c.hostStart((uint16_t)port)) { std::cerr << c.error() << "\n"; return BAD_SOCK; }
        std::cout << "waiting for a player on";
        for (const std::string& a : localAddresses()) std::cout << " " << a << ":" << port;
        std::cout << "\n";
    } else if (!c.joinStart(ip, (uint16_t)port)) {
        std::cerr << c.error() << "\n";
        return BAD_SOCK;
    } else {
        std::cout << "connecting to " << ip << ":" << port << "\n";
    }

    auto last = std::chrono::steady_clock::now();
    while (c.poll() == ConnState::Waiting) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        if (std::chrono::steady_clock::now() - last > std::chrono::seconds(1)) {
            std::cout << "." << std::flush;
            last = std::chrono::steady_clock::now();
        }
    }
    std::cout << "\n";
    if (c.state() == ConnState::Failed) { std::cerr << "connection failed: " << c.error() << "\n"; return BAD_SOCK; }
    return c.take();
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "";
    bool cliHost = mode == "host" && (argc == 3 || argc == 4);
    bool cliJoin = mode == "join" && (argc == 4 || argc == 5);
    if (argc > 1 && !cliHost && !cliJoin) {
        std::cerr << "usage: battleship host <port> [name] | battleship join <ip> <port> [name] | battleship\n";
        return 1;
    }
    if (!netInit()) { std::cerr << "network init failed\n"; return 1; }

    bool isHost = true;
    std::string name = "Player";
    sock_t s;
    if (cliHost) {
        isHost = true;
        if (argc == 4) name = argv[3];
        std::cout << "waiting for a player on port " << argv[2] << "\n";
        s = hostAndWait((uint16_t)std::stoi(argv[2]));
    } else if (cliJoin) {
        isHost = false;
        if (argc == 5) name = argv[4];
        s = joinPeer(argv[2], (uint16_t)std::stoi(argv[3]));
    } else {
        s = connectInteractive(isHost, name);
    }
    if (s == BAD_SOCK) { std::cerr << "connection failed\n"; netShutdown(); return 1; }
    prepareSocket(s);
    std::cout << "connected to " << peerAddress(s) << (isHost ? " (you are the host, you fire first)\n" : "\n");

    DemoFleet fleet((unsigned)std::time(nullptr) ^ (isHost ? 0x5a5a : 0xa5a5));
    PeerGame game(isHost, name, fleet);
    game.onConnected();
    std::cout << "your fleet:\n" << fleet.render()
              << "commands: ready | fire B7 (or just B7) | board | enemy | quit\n";

    std::thread(readStdin).detach();
    LineReader reader;
    bool open = true, quit = false;

    while (open) {
        if (!reader.pump(s)) {
            std::string line;
            while (reader.next(line)) { std::cout << "< " << line << "\n"; game.onLine(line); }
            game.onDisconnect();
            open = false;
        }
        std::string line;
        while (reader.next(line)) {
            std::cout << "< " << line << "\n";
            game.onLine(line);
        }

        while (popInput(line)) {
            std::string cmd = line;
            for (char& ch : cmd) ch = (char)std::tolower((unsigned char)ch);
            int r, c;
            if (cmd == "ready") game.onAllPlaced();
            else if (cmd == "board") std::cout << fleet.render();
            else if (cmd == "enemy") std::cout << renderEnemy(game);
            else if (cmd == "quit") { game.quit(); quit = true; }
            else if (cmd.rfind("fire ", 0) == 0 && parseCell(cmd.substr(5), r, c)) game.fire(r, c);
            else if (parseCell(cmd, r, c)) game.fire(r, c);
            else if (!cmd.empty()) std::cout << "? commands: ready | fire B7 | board | enemy | quit\n";
        }

        for (const std::string& out : game.takeOutgoing()) {
            std::cout << "> " << out << "\n";
            if (open && !sendLine(s, out)) { game.onDisconnect(); open = false; }
        }
        for (const std::string& e : game.takeEvents()) std::cout << "* " << e << "\n";

        if (game.state() == State::GameOver) open = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << (quit ? "YOU QUIT" : game.won() ? "YOU WON" : game.peerLeft() ? "OPPONENT LEFT" : "YOU LOST")
              << " (" << game.shotsHit() << "/" << game.shotsFired() << " shots hit)\n";
    closeSock(s);
    netShutdown();
    return 0;
}
