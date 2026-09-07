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

add_executable(Cladia
    src/main.cpp
)

target_link_libraries(Cladia PRIVATE SDL3::SDL3)

add_custom_command(
    TARGET Cladia
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:Cladia>
)