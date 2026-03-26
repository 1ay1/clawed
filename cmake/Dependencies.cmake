include(FetchContent)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# ── simdjson ─────────────────────────────────────────────────────────────────
FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.12.2
    GIT_SHALLOW    TRUE
)

# ── nlohmann/json ────────────────────────────────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# ── FTXUI ────────────────────────────────────────────────────────────────────
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v5.0.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(simdjson nlohmann_json ftxui)
