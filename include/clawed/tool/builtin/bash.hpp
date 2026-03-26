#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace clawed::builtin {

struct BashTool {
    static constexpr auto name() -> std::string_view { return "bash"; }

    static constexpr auto description() -> std::string_view {
        return "Execute a bash command and return its stdout/stderr. "
               "Use this for running shell commands, scripts, and system operations.";
    }

    static auto input_schema() -> nlohmann::json {
        return {
            {"type", "object"},
            {"required", {"command"}},
            {"properties", {
                {"command", {
                    {"type", "string"},
                    {"description", "The bash command to execute"}
                }},
                {"timeout", {
                    {"type", "integer"},
                    {"description", "Timeout in seconds (default 120)"}
                }}
            }}
        };
    }

    auto execute(const nlohmann::json& input) const -> Result<std::string>;

    std::filesystem::path working_dir = std::filesystem::current_path();
    std::chrono::seconds  default_timeout{120};
};

} // namespace clawed::builtin
