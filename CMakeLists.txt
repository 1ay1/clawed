cmake_minimum_required(VERSION 3.24)
project(clawed VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ─────────────────────────────────────────────────────────────
include(cmake/Dependencies.cmake)

# ── Main executable ──────────────────────────────────────────────────────────
add_executable(clawed
    src/main.cpp
    src/auth/auth.cpp
    src/api/client.cpp
    src/api/sse_parser.cpp
    src/tool/registry.cpp
    src/agent/loop.cpp
    src/agent/conversation.cpp
    src/tui/app.cpp
    src/tui/stream_view.cpp
    src/tui/input_view.cpp
    src/tui/tool_view.cpp
    src/ssh/session.cpp
    src/executor/local.cpp
    src/executor/ssh.cpp
    src/ide/ide_app.cpp
)

target_include_directories(clawed PRIVATE include)
target_link_libraries(clawed PRIVATE
    CURL::libcurl
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    ftxui::screen
    ftxui::dom
    ftxui::component
)

target_compile_options(clawed PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    $<$<CONFIG:Release>:-O3 -march=native -flto>
    $<$<CONFIG:Debug>:-g -fsanitize=address,undefined>
)
target_link_options(clawed PRIVATE
    $<$<CONFIG:Release>:-flto>
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
