//dedicated server: accept two clients, run Game, print a log
//
//usage: battleship_server [port=5000] [turn_timeout_sec=0]
//test:  two terminals running  nc localhost 5000, or two GUI clients
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "game_server.h"

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

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 5000;
    double timeout = argc > 2 ? std::atof(argv[2]) : 0;
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "usage: %s [port=5000] [turn_timeout_sec=0]\n", argv[0]);
        return 1;
    }
    if (!netInit()) { std::fprintf(stderr, "network init failed\n"); return 1; }

    GameServer server(timeout);
    if (!server.start((uint16_t)port)) {
        std::fprintf(stderr, "%s\n", server.error().c_str());
        return 1;
    }
    for (const std::string& l : server.takeLog()) logLine(l);
    logLine(timeout > 0 ? "turn timeout " + std::to_string((int)timeout) + "s" : "no turn timeout");
    for (const std::string& ip : localAddresses()) logLine("players can join " + ip + ":" + std::to_string(port));

    for (;;) {
        server.step(200);   //wake up 5x a second so the turn timeout can fire
        for (const std::string& l : server.takeLog()) logLine(l);
    }
}
