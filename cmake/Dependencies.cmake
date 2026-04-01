include(FetchContent)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# ── nlohmann/json ────────────────────────────────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(nlohmann_json)
