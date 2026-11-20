# Quick way to build and run without CMake.
#   make test
CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1 -g

LOGIC = common/rules.cpp server/board.cpp
FLOW = $(LOGIC) server/game.cpp
FLOW_H = common/rules.h server/board.h server/game.h
NET = common/net.cpp server/game_server.cpp
NET_H = common/net.h server/game_server.h
CLIENT = $(wildcard client/*.cpp)
CLIENT_H = $(wildcard client/*.h)

# RAYLIB = folder of the raylib release zip (has include/ and lib/), empty if raylib is installed system-wide
RAYLIB ?=
ifneq ($(RAYLIB),)
  RAYLIB_FLAGS = -I$(RAYLIB)/include -L$(RAYLIB)/lib
endif

ifeq ($(OS),Windows_NT)
  NET_LIBS = -lws2_32 -liphlpapi -static   # -static: friends don't need MinGW's DLLs
  RAYLIB_LIBS ?= -lraylib -lopengl32 -lgdi32 -lwinmm
else ifeq ($(shell uname),Darwin)
  NET_LIBS = -pthread
  RAYLIB_LIBS ?= -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
else
  NET_LIBS = -pthread
  RAYLIB_LIBS ?= -lraylib -lGL -lm -ldl -lrt -lX11
endif

#   make test        board + game flow + networking tests
#   make test-game   game flow tests only
#   make server      dedicated server -> ./build/battleship_server [port] [turn_timeout_sec]
#   make client      the game (GUI)   -> ./build/battleship host <port> | join <ip> <port>
test: build/test_board build/test_game build/test_net
	./build/test_board
	./build/test_game
	./build/test_net

test-game: build/test_game
	./build/test_game

server: build/battleship_server

client: build/battleship

build/test_board: tests/test_board.cpp $(LOGIC) common/rules.h server/board.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ tests/test_board.cpp $(LOGIC)

build/test_game: tests/test_game.cpp $(FLOW) $(FLOW_H)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ tests/test_game.cpp $(FLOW)

build/test_net: tests/test_net.cpp $(FLOW) $(FLOW_H) $(NET) $(NET_H)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ tests/test_net.cpp $(FLOW) $(NET) $(NET_LIBS)

build/battleship_server: server/main.cpp $(FLOW) $(FLOW_H) $(NET) $(NET_H)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ server/main.cpp $(FLOW) $(NET) $(NET_LIBS)

build/battleship: $(CLIENT) $(CLIENT_H) $(FLOW) $(FLOW_H) $(NET) $(NET_H)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(RAYLIB_FLAGS) -o $@ $(CLIENT) $(FLOW) $(NET) $(RAYLIB_LIBS) $(NET_LIBS)

clean:
	rm -rf build

.PHONY: test test-game server client clean
