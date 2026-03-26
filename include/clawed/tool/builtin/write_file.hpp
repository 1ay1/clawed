#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace clawed::builtin {

struct WriteFileTool {
    static constexpr auto name() -> std::string_view { return "write_file"; }

    static constexpr auto description() -> std::string_view {
        return "Write content to a file. Creates the file if it doesn't exist, "
               "overwrites if it does.";
    }

    static auto input_schema() -> nlohmann::json {
        return {
            {"type", "object"},
            {"required", {"file_path", "content"}},
            {"properties", {
                {"file_path", {
                    {"type", "string"},
                    {"description", "Absolute path to the file to write"}
                }},
                {"content", {
                    {"type", "string"},
                    {"description", "The content to write to the file"}
                }}
            }}
        };
    }

    auto execute(const nlohmann::json& input) const -> Result<std::string>;
};

} // namespace clawed::builtin
