cmake_minimum_required (VERSION 2.6)

project(mcpelaunchersnaplook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)

include(FetchContent)

FetchContent_Declare(libhat
        GIT_REPOSITORY https://github.com/BasedInc/libhat.git
        GIT_TAG 69149a1cee5a0228492826bd71af1a32d73ed87e
        EXCLUDE_FROM_ALL)

FetchContent_Declare(properties-parser
        GIT_REPOSITORY https://github.com/minecraft-linux/properties-parser.git
        GIT_TAG 79f5360f889100bd4a1e069f4d9b86e94c902633
        EXCLUDE_FROM_ALL)


FetchContent_MakeAvailable(libhat properties-parser)

find_library(log-lib log android-lib android)

add_library(mcpelaunchersnaplook SHARED src/main.cpp src/main.h src/conf.cpp src/conf.h src/imgui.cpp src/imgui.h src/key_mapping.h src/main.cpp src/main.h src/snaplook.cpp src/snaplook.h src/util.cpp src/util.h)
target_link_libraries(mcpelaunchersnaplook PUBLIC ${log-lib} android libhat::libhat properties-parser)
