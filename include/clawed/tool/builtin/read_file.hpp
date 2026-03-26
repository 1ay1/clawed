#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace clawed::builtin {

struct ReadFileTool {
    static constexpr auto name() -> std::string_view { return "read_file"; }

    static constexpr auto description() -> std::string_view {
        return "Read the contents of a file. Returns the file content with line numbers.";
    }

    static auto input_schema() -> nlohmann::json {
        return {
            {"type", "object"},
            {"required", {"file_path"}},
            {"properties", {
                {"file_path", {
                    {"type", "string"},
                    {"description", "Absolute path to the file to read"}
                }},
                {"offset", {
                    {"type", "integer"},
                    {"description", "Line number to start reading from (1-based)"}
                }},
                {"limit", {
                    {"type", "integer"},
                    {"description", "Maximum number of lines to read"}
                }}
            }}
        };
    }

    auto execute(const nlohmann::json& input) const -> Result<std::string>;
};

} // namespace clawed::builtin
