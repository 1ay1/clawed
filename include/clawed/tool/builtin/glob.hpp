#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace clawed::builtin {

struct GlobTool {
    static constexpr auto name() -> std::string_view { return "glob"; }

    static constexpr auto description() -> std::string_view {
        return "Find files matching a glob pattern. Returns matching file paths.";
    }

    static auto input_schema() -> nlohmann::json {
        return {
            {"type", "object"},
            {"required", {"pattern"}},
            {"properties", {
                {"pattern", {
                    {"type", "string"},
                    {"description", "Glob pattern to match (e.g. '**/*.cpp')"}
                }},
                {"path", {
                    {"type", "string"},
                    {"description", "Directory to search in (default: cwd)"}
                }}
            }}
        };
    }

    auto execute(const nlohmann::json& input) const -> Result<std::string>;
};

} // namespace clawed::builtin
