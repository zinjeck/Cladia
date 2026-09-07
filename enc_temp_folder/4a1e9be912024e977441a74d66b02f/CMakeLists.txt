cmake_minimum_required(VERSION 3.28)

project(Cladia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.20
)

FetchContent_MakeAvailable(SDL3)

set(SDLTTF_VENDORED ON CACHE BOOL "Build SDL_ttf dependencies from source" FORCE)
FetchContent_Declare(
    SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG release-3.2.2
)

FetchContent_MakeAvailable(SDL3_ttf)

add_executable(Cladia
    src/main.cpp
    src/AquaticWorld.cpp
    src/AquaticWorld.h
    src/SimulationClock.cpp
    src/SimulationClock.h
    src/CellSystem.cpp
    src/CellSystem.h
    src/SpeciesRegistry.cpp
    src/SpeciesRegistry.h
)

target_link_libraries(Cladia PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)

add_custom_command(
    TARGET Cladia
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:Cladia>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
        $<TARGET_FILE_DIR:Cladia>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/fonts/Arimo-Regular.ttf
        $<TARGET_FILE_DIR:Cladia>/Arimo-Regular.ttf
)
