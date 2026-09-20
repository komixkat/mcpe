cmake_minimum_required(VERSION 3.15)

# Discord Rich Presence for the launcher/game (custom/mods/discordrpc).
#
# Self-contained by design: unlike the signature-hooking mods this one never
# touches game internals, so it keeps working across game updates without any
# rebuild. Run the game with Discord desktop open and a client_id set in
# discordrpc.conf to see presence.
project(mcpelauncherdiscordrpc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(mcpelauncherdiscordrpc SHARED
        src/main.cpp
        src/presence.cpp
        src/discord_ipc.cpp)

target_include_directories(mcpelauncherdiscordrpc PRIVATE src)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(mcpelauncherdiscordrpc PRIVATE -O3 -flto)
    target_link_options(mcpelauncherdiscordrpc PRIVATE -flto)
endif()

install(TARGETS mcpelauncherdiscordrpc LIBRARY DESTINATION mods)