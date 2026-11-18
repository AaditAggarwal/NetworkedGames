//thin test server for the game flow: accept two clients, poll() loop, feed Game, print a log
//STAND-IN until role 1's networking layer (socket helpers, LineReader, server/log.*) lands here.
//only the socket code should change then, Game is the part that stays
//
//usage: battleship_server [port=5000] [turn_timeout_sec=0]
//test:  two terminals running  nc localhost 5000
//POSIX only (Linux / WSL2 / macOS), like the plan says
#ifdef _WIN32
#error "build the test server on Linux or WSL2 (see the project plan, section 6)"
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "game.h"

static const size_t MAX_BUF = 64 * 1024;   //a client that sends this much without \n is dropped

struct Slot {
    int fd = -1;
    std::string addr;
    std::string buf;   //bytes received but not yet split into lines
};

static Slot slots[2];

static double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

//"[14:03:22.481] text", multi-line text gets every line indented under the stamp
static void logLine(const std::string& text) {
    using namespace std::chrono;
    auto t = system_clock::now();
    std::time_t secs = system_clock::to_time_t(t);
    int ms = (int)(duration_cast<milliseconds>(t.time_since_epoch()).count() % 1000);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", std::localtime(&secs));
    std::string body = text;
    while (!body.empty() && body.back() == '\n') body.pop_back();
    std::string indented;
    for (char c : body) {
        indented += c;
        if (c == '\n') indented += "               ";
    }
    std::printf("[%s.%03d] %s\n", stamp, ms, indented.c_str());
    std::fflush(stdout);
}

static std::string addrOf(const sockaddr_in& a) {
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
    return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}

static bool sendAll(int fd, const std::string& line) {
    std::string msg = line + "\n";
    size_t off = 0;
    while (off < msg.size()) {
        ssize_t n = send(fd, msg.data() + off, msg.size() - off, 0);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

static void closeSlot(int p) {
    if (slots[p].fd >= 0) close(slots[p].fd);   //close() sends FIN
    slots[p] = Slot{};
}

//drain everything Game queued: log, then messages, then closes (order matters, GAME_OVER
//and OPP_LEFT must go out before the FIN)
static void flush(Game& game) {
    for (const std::string& l : game.takeLog()) logLine("  * " + l);
    for (const OutMsg& m : game.takeOutgoing()) {
        if (slots[m.player].fd < 0) continue;
        logLine("> P" + std::to_string(m.player + 1) + " " + m.text);
        if (!sendAll(slots[m.player].fd, m.text)) logLine("  ! send to P" + std::to_string(m.player + 1) + " failed");
    }
    for (int p : game.takeKicks()) {
        if (slots[p].fd < 0) continue;
        logLine("closing P" + std::to_string(p + 1) + " " + slots[p].addr);
        closeSlot(p);
    }
}

//read what arrived on slot p and feed every complete line to the game
static void readSlot(Game& game, int p) {
    char tmp[1024];
    ssize_t n = recv(slots[p].fd, tmp, sizeof tmp, 0);
    if (n < 0 && errno == EINTR) return;
    if (n <= 0 || slots[p].buf.size() + (size_t)n > MAX_BUF) {
        logLine(std::string("P") + std::to_string(p + 1) + " " + slots[p].addr +
                (n == 0 ? " closed the connection (recv returned 0)" : n < 0 ? " recv error" : " sent too much without a newline"));
        closeSlot(p);
        game.onDisconnect(p);
        flush(game);
        return;
    }
    slots[p].buf.append(tmp, (size_t)n);

    int fd = slots[p].fd;
    size_t nl;
    while (slots[p].fd == fd && (nl = slots[p].buf.find('\n')) != std::string::npos) {
        std::string line = slots[p].buf.substr(0, nl);
        slots[p].buf.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        logLine("< P" + std::to_string(p + 1) + " " + line);
        game.onLine(p, line);
        flush(game);   //may kick this slot (game over), the loop condition stops then
    }
}

static void acceptClient(Game& game, int ls) {
    sockaddr_in a{};
    socklen_t len = sizeof a;
    int fd = accept(ls, (sockaddr*)&a, &len);
    if (fd < 0) return;
    std::string addr = addrOf(a);

    int p = game.freeSlot();
    if (p < 0 || slots[p].fd >= 0) {
        logLine("rejecting " + addr + ", server full");
        sendAll(fd, "ERR server full");
        close(fd);
        return;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);   //send each line right away
    slots[p].fd = fd;
    slots[p].addr = addr;
    logLine("accepted " + addr + " as P" + std::to_string(p + 1));
    game.onConnect(p, addr);
    flush(game);
}

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 5000;
    double timeout = argc > 2 ? std::atof(argv[2]) : 0;
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "usage: %s [port=5000] [turn_timeout_sec=0]\n", argv[0]);
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);   //send() to a closed peer returns an error instead of killing us

    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (ls < 0 || bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, 4) != 0) {
        std::perror("listen");
        return 1;
    }

    Game game(timeout);
    logLine("listening on port " + std::to_string(port) +
            (timeout > 0 ? ", turn timeout " + std::to_string((int)timeout) + "s" : ", no turn timeout"));

    for (;;) {
        pollfd fds[3];
        fds[0] = {ls, POLLIN, 0};
        for (int p = 0; p < 2; ++p) fds[p + 1] = {slots[p].fd, POLLIN, 0};   //poll() skips fd -1

        int n = poll(fds, 3, 200);   //wake up 5x a second so the turn timeout can fire
        if (n < 0 && errno != EINTR) { std::perror("poll"); return 1; }

        game.tick(nowSec());
        flush(game);

        //clients first, a slot closed here may be reused by accept below
        for (int p = 0; p < 2; ++p)
            if (slots[p].fd >= 0 && slots[p].fd == fds[p + 1].fd && (fds[p + 1].revents & (POLLIN | POLLHUP | POLLERR)))
                readSlot(game, p);
        if (fds[0].revents & POLLIN) acceptClient(game, ls);
    }
}
