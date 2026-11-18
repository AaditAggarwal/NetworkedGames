cmake_minimum_required(VERSION 3.16)
project(battleship CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(core STATIC
  common/rules.cpp common/net.cpp
  server/board.cpp server/game.cpp server/game_server.cpp)
target_link_libraries(core PUBLIC Threads::Threads)
if (WIN32)
  target_link_libraries(core PUBLIC ws2_32 iphlpapi)
  target_compile_definitions(core PUBLIC _WIN32_WINNT=0x0601)
endif()
if (MINGW)
  target_link_options(core INTERFACE -static)   # friends don't need MinGW's DLLs
endif()

add_executable(battleship_server server/main.cpp)
target_link_libraries(battleship_server PRIVATE core)

foreach(t test_board test_game test_net)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE core)
endforeach()

# the GUI needs raylib: installed, or downloaded once with -DFETCH_RAYLIB=ON
option(FETCH_RAYLIB "download and build raylib 5.5" OFF)
if (FETCH_RAYLIB)
  include(FetchContent)
  FetchContent_Declare(raylib URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(raylib)
else()
  find_package(raylib QUIET)
endif()

if (TARGET raylib)
  file(GLOB CLIENT_SRC client/*.cpp)
  add_executable(battleship ${CLIENT_SRC})
  target_link_libraries(battleship PRIVATE core raylib)
else()
  message(STATUS "raylib not found, skipping the GUI (use -DFETCH_RAYLIB=ON)")
endif()
