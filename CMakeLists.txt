cmake_minimum_required(VERSION 3.16)

project(chess_with_codex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CHESS_ICON_PNG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png")
set(CHESS_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.icns")

add_custom_target(chess_icon_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CHESS_ICON_PNG}"
            "${CMAKE_CURRENT_BINARY_DIR}/icon.png"
    DEPENDS "${CHESS_ICON_PNG}" "${CHESS_APP_ICON}"
    VERBATIM
)

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
    src/evaluation.cpp
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

if(APPLE)
    add_custom_target(chess_cli_icon ALL
        COMMAND /bin/sh
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/apply_macos_file_icon.sh"
                "${CHESS_ICON_PNG}"
                "$<TARGET_FILE:chess>"
                "${CMAKE_CURRENT_BINARY_DIR}/cli-icon"
        DEPENDS chess "${CHESS_ICON_PNG}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/apply_macos_file_icon.sh"
        VERBATIM
    )
endif()

add_executable(chess_tests
    tests/engine_tests.cpp
)

target_link_libraries(chess_tests PRIVATE chess_bots)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
    set_source_files_properties("${CHESS_APP_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )

    add_executable(chess_gui MACOSX_BUNDLE
        src/gui.mm
        "${CHESS_APP_ICON}"
    )

    target_link_libraries(chess_gui PRIVATE
        chess_bots
        "-framework AppKit"
        "-framework QuartzCore"
    )

    target_compile_options(chess_gui PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )

    set_target_properties(chess_gui PROPERTIES
        OUTPUT_NAME "Chess"
        MACOSX_BUNDLE_BUNDLE_NAME "Chess"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.codex.chess-with-codex"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    )
endif()
