cmake_minimum_required(VERSION 3.16)
project(battleship CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(p2p STATIC src/net.cpp src/peer_game.cpp src/demo_fleet.cpp)
target_link_libraries(p2p PUBLIC Threads::Threads)
if (WIN32)
  target_link_libraries(p2p PUBLIC ws2_32 iphlpapi)
  target_compile_definitions(p2p PUBLIC _WIN32_WINNT=0x0601)
endif()

add_executable(battleship src/main.cpp)
target_link_libraries(battleship PRIVATE p2p)

add_executable(test_peer tests/test_peer.cpp)
target_link_libraries(test_peer PRIVATE p2p)
