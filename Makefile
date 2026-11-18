# Quick way to build and run the game-logic unit tests without CMake.
#   make test
CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1 -g

LOGIC = common/rules.cpp server/board.cpp

test: build/test_board
	./build/test_board

build/test_board: tests/test_board.cpp $(LOGIC) common/rules.h server/board.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ tests/test_board.cpp $(LOGIC)

clean:
	rm -rf build

.PHONY: test clean
