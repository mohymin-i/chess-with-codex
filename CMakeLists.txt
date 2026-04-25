cmake_minimum_required(VERSION 3.16)

project(chess_with_codex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE MACOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(EXISTS "${MACOS_SDK_PATH}/usr/include/c++/v1/array")
        add_compile_options(-isystem "${MACOS_SDK_PATH}/usr/include/c++/v1")
    endif()
endif()

add_library(chess_engine
    src/board.cpp
)

target_include_directories(chess_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(chess_bots
    src/bot.cpp
)

target_include_directories(chess_bots PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(chess_bots PUBLIC chess_engine)

add_executable(chess
    src/cli.cpp
    src/main.cpp
)

target_link_libraries(chess PRIVATE chess_bots)

add_executable(chess_tests
    tests/engine_tests.cpp
)

target_link_libraries(chess_tests PRIVATE chess_bots)
