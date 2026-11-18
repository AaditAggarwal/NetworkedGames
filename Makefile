# Quick way to build and run the game-logic unit tests without CMake.
#   make test
CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1 -g

LOGIC = common/rules.cpp server/board.cpp
FLOW = $(LOGIC) server/game.cpp
FLOW_H = common/rules.h server/board.h server/game.h

#   make test        board + game flow tests
#   make test-game   game flow tests only
#   make server      thin test server -> ./build/battleship_server [port] [turn_timeout_sec]
test: build/test_board build/test_game
	./build/test_board
	./build/test_game

test-game: build/test_game
	./build/test_game

server: build/battleship_server

build/test_board: tests/test_board.cpp $(LOGIC) common/rules.h server/board.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ tests/test_board.cpp $(LOGIC)

build/test_game: tests/test_game.cpp $(FLOW) $(FLOW_H)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ tests/test_game.cpp $(FLOW)

build/battleship_server: server/main.cpp $(FLOW) $(FLOW_H)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ server/main.cpp $(FLOW)

clean:
	rm -rf build

.PHONY: test test-game server clean
