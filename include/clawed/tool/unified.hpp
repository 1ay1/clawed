#pragma once

// ── Unified tool definitions ─────────────────────────────────────────────────
// Each tool delegates to an AnyExecutor. One implementation works for both
// local and SSH execution — the transport is abstracted away.
//
// Adding a new tool:
//   1. Define a struct with name(), description(), input_schema(), execute()
//   2. Call executor_->whatever() in execute()
//   3. Register it in main.cpp
// That's it.

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <clawed/executor/concept.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>

namespace clawed::tools {

// ── BashTool ─────────────────────────────────────────────────────────────────

struct Bash {
    std::shared_ptr<AnyExecutor> executor;

    static constexpr auto name() -> std::string_view { return "bash"; }

    static constexpr auto description() -> std::string_view {
        return "Execute a bash command and return stdout/stderr. "
               "Use this for shell commands, scripts, and system operations.";
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

    auto execute(const nlohmann::json& input) const -> Result<std::string> {
        if (!input.contains("command") || !input["command"].is_string())
            return make_error(ErrorCode::ToolExecFailed, "missing 'command' parameter");

        auto cmd = input["command"].get<std::string>();
        auto timeout = std::chrono::seconds{120};
        if (input.contains("timeout") && input["timeout"].is_number_integer())
            timeout = std::chrono::seconds(input["timeout"].get<int>());

        return executor->run_command(cmd, timeout);
    }
};

// ── ReadFileTool ─────────────────────────────────────────────────────────────

struct ReadFile {
    std::shared_ptr<AnyExecutor> executor;

    static constexpr auto name() -> std::string_view { return "read_file"; }

    static constexpr auto description() -> std::string_view {
        return "Read file contents with line numbers. "
               "Supports offset and limit for large files.";
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
                    {"description", "Line number to start from (1-based, default 1)"}
                }},
                {"limit", {
                    {"type", "integer"},
                    {"description", "Max lines to read (default 2000)"}
                }}
            }}
        };
    }

    auto execute(const nlohmann::json& input) const -> Result<std::string> {
        if (!input.contains("file_path") || !input["file_path"].is_string())
            return make_error(ErrorCode::ToolExecFailed, "missing 'file_path' parameter");

        auto path = input["file_path"].get<std::string>();
        int offset = 1, limit = 2000;
        if (input.contains("offset") && input["offset"].is_number_integer())
            offset = std::max(1, input["offset"].get<int>());
        if (input.contains("limit") && input["limit"].is_number_integer())
            limit = input["limit"].get<int>();

        return executor->read_file(path, offset, limit);
    }
};

// ── WriteFileTool ────────────────────────────────────────────────────────────

struct WriteFile {
    std::shared_ptr<AnyExecutor> executor;

    static constexpr auto name() -> std::string_view { return "write_file"; }

    static constexpr auto description() -> std::string_view {
        return "Write content to a file. Creates parent directories if needed. "
               "Overwrites existing files.";
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
                    {"description", "Content to write to the file"}
                }}
            }}
        };
    }

    auto execute(const nlohmann::json& input) const -> Result<std::string> {
        if (!input.contains("file_path") || !input["file_path"].is_string())
            return make_error(ErrorCode::ToolExecFailed, "missing 'file_path' parameter");
        if (!input.contains("content") || !input["content"].is_string())
            return make_error(ErrorCode::ToolExecFailed, "missing 'content' parameter");

        return executor->write_file(
            input["file_path"].get<std::string>(),
            input["content"].get<std::string>());
    }
};

// ── GlobTool ─────────────────────────────────────────────────────────────────

struct Glob {
    std::shared_ptr<AnyExecutor> executor;

    static constexpr auto name() -> std::string_view { return "glob"; }

    static constexpr auto description() -> std::string_view {
        return "Find files matching a glob pattern. "
               "Supports ** for recursive matching.";
    }

    static auto input_schema() -> nlohmann::json {
        return {
            {"type", "object"},
            {"required", {"pattern"}},
            {"properties", {
                {"pattern", {
                    {"type", "string"},
                    {"description", "Glob pattern (e.g. '**/*.cpp', 'src/*.hpp')"}
                }},
                {"path", {
                    {"type", "string"},
                    {"description", "Directory to search in (default: working directory)"}
                }}
            }}
        };
    }

    auto execute(const nlohmann::json& input) const -> Result<std::string> {
        if (!input.contains("pattern") || !input["pattern"].is_string())
            return make_error(ErrorCode::ToolExecFailed, "missing 'pattern' parameter");

        std::string dir;
        if (input.contains("path") && input["path"].is_string())
            dir = input["path"].get<std::string>();

        return executor->find_files(input["pattern"].get<std::string>(), dir);
    }
};

// ── GrepTool ─────────────────────────────────────────────────────────────────

struct Grep {
    std::shared_ptr<AnyExecutor> executor;

    static constexpr auto name() -> std::string_view { return "grep"; }

    static constexpr auto description() -> std::string_view {
        return "Search file contents with regex patterns. "
               "Uses ripgrep if available, falls back to grep. "
               "Returns matches with file path, line number, and content.";
    }

    static auto input_schema() -> nlohmann::json {
        return {
            {"type", "object"},
            {"required", {"pattern"}},
            {"properties", {
                {"pattern", {
                    {"type", "string"},
                    {"description", "Regex pattern to search for"}
                }},
                {"path", {
                    {"type", "string"},
                    {"description", "File or directory to search (default: working directory)"}
                }},
                {"max_results", {
                    {"type", "integer"},
                    {"description", "Maximum matches to return (default 200)"}
                }}
            }}
        };
    }

    auto execute(const nlohmann::json& input) const -> Result<std::string> {
        if (!input.contains("pattern") || !input["pattern"].is_string())
            return make_error(ErrorCode::ToolExecFailed, "missing 'pattern' parameter");

        std::string path;
        if (input.contains("path") && input["path"].is_string())
            path = input["path"].get<std::string>();

        int max_results = 200;
        if (input.contains("max_results") && input["max_results"].is_number_integer())
            max_results = input["max_results"].get<int>();

        return executor->grep(input["pattern"].get<std::string>(), path, max_results);
    }
};

// ── EditTool ─────────────────────────────────────────────────────────────────

struct Edit {
    std::shared_ptr<AnyExecutor> executor;

    static constexpr auto name() -> std::string_view { return "edit"; }

    static constexpr auto description() -> std::string_view {
        return "Make a surgical text replacement in a file. "
               "The old_string must be unique in the file. "
               "Provide enough surrounding context to ensure uniqueness.";
    }

    static auto input_schema() -> nlohmann::json {
        return {
            {"type", "object"},
            {"required", {"file_path", "old_string", "new_string"}},
            {"properties", {
                {"file_path", {
                    {"type", "string"},
                    {"description", "Absolute path to the file to edit"}
                }},
                {"old_string", {
                    {"type", "string"},
                    {"description", "The exact text to find and replace (must be unique)"}
                }},
                {"new_string", {
                    {"type", "string"},
                    {"description", "The replacement text"}
                }}
            }}
        };
    }

    auto execute(const nlohmann::json& input) const -> Result<std::string> {
        if (!input.contains("file_path") || !input["file_path"].is_string())
            return make_error(ErrorCode::ToolExecFailed, "missing 'file_path' parameter");
        if (!input.contains("old_string") || !input["old_string"].is_string())
            return make_error(ErrorCode::ToolExecFailed, "missing 'old_string' parameter");
        if (!input.contains("new_string") || !input["new_string"].is_string())
            return make_error(ErrorCode::ToolExecFailed, "missing 'new_string' parameter");

        return executor->edit_file(
            input["file_path"].get<std::string>(),
            input["old_string"].get<std::string>(),
            input["new_string"].get<std::string>());
    }
};

// ── Register all tools ───────────────────────────────────────────────────────
// Convenience function to register the full tool suite on any executor.

template <typename Registry>
void register_all(Registry& registry, std::shared_ptr<AnyExecutor> executor) {
    registry.register_tool(Bash{executor});
    registry.register_tool(ReadFile{executor});
    registry.register_tool(WriteFile{executor});
    registry.register_tool(Glob{executor});
    registry.register_tool(Grep{executor});
    registry.register_tool(Edit{executor});
}

} // namespace clawed::tools
