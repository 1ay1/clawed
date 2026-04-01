#pragma once

// ── Tool suite ───────────────────────────────────────────────────────────────
//
// Each tool is a ToolDef<> subtype. The entire definition is:
//   1. Name + description       (what it is)
//   2. Parameters as fields     (the schema IS the type)
//   3. A run() function         (the logic — receives typed, validated args)
//
// No JSON extraction boilerplate. No hand-written schemas.
// Adding a new tool = ~12 lines. The framework handles everything else.
//
// register_all() registers the full suite on any executor — local or SSH.

#include <clawed/tool/tool_def.hpp>
#include <clawed/tool/field.hpp>
#include <clawed/executor/concept.hpp>

#include <chrono>
#include <string>

namespace clawed::tools {

// ── bash ─────────────────────────────────────────────────────────────────────

struct Bash : ToolDef<Bash> {
    static constexpr std::string_view tool_name = "bash";
    static constexpr std::string_view tool_desc =
        "Execute a bash command and return stdout/stderr.";
    static constexpr auto params = std::tuple{
        Required<std::string>{"command", "The bash command to execute"},
        Optional<int>{"timeout", "Timeout in seconds", 120},
    };
    static auto run(AnyExecutor& exec, std::string command, int timeout)
        -> Result<std::string>
    {
        return exec.run_command(command, std::chrono::seconds{timeout});
    }
};

// ── read_file ────────────────────────────────────────────────────────────────

struct ReadFile : ToolDef<ReadFile> {
    static constexpr std::string_view tool_name = "read_file";
    static constexpr std::string_view tool_desc =
        "Read file contents with line numbers. "
        "If path is a directory, lists its contents.";
    static constexpr auto params = std::tuple{
        Required<std::string>{"file_path", "Absolute path to the file to read"},
        Optional<int>{"offset", "Line number to start from (1-based)", 1},
        Optional<int>{"limit", "Max lines to read", 2000},
    };
    static auto run(AnyExecutor& exec, std::string path, int offset, int limit)
        -> Result<std::string>
    {
        return exec.read_file(path, offset, limit);
    }
};

// ── write_file ───────────────────────────────────────────────────────────────

struct WriteFile : ToolDef<WriteFile> {
    static constexpr std::string_view tool_name = "write_file";
    static constexpr std::string_view tool_desc =
        "Write content to a file. Creates parent directories. Overwrites existing files.";
    static constexpr auto params = std::tuple{
        Required<std::string>{"file_path", "Absolute path to the file"},
        Required<std::string>{"content", "Content to write"},
    };
    static auto run(AnyExecutor& exec, std::string path, std::string content)
        -> Result<std::string>
    {
        return exec.write_file(path, content);
    }
};

// ── glob ─────────────────────────────────────────────────────────────────────

struct Glob : ToolDef<Glob> {
    static constexpr std::string_view tool_name = "glob";
    static constexpr std::string_view tool_desc =
        "Find files matching a glob pattern. Supports ** for recursive matching.";
    static constexpr auto params = std::tuple{
        Required<std::string>{"pattern", "Glob pattern (e.g. '**/*.cpp')"},
        Optional<std::string>{"path", "Directory to search in", {}},
    };
    static auto run(AnyExecutor& exec, std::string pattern, std::string path)
        -> Result<std::string>
    {
        return exec.find_files(pattern, path);
    }
};

// ── grep ─────────────────────────────────────────────────────────────────────

struct Grep : ToolDef<Grep> {
    static constexpr std::string_view tool_name = "grep";
    static constexpr std::string_view tool_desc =
        "Search file contents with regex. Uses ripgrep if available.";
    static constexpr auto params = std::tuple{
        Required<std::string>{"pattern", "Regex pattern to search for"},
        Optional<std::string>{"path", "File or directory to search", {}},
        Optional<int>{"max_results", "Maximum matches to return", 200},
    };
    static auto run(AnyExecutor& exec, std::string pattern, std::string path, int max)
        -> Result<std::string>
    {
        return exec.grep(pattern, path, max);
    }
};

// ── edit ─────────────────────────────────────────────────────────────────────

struct Edit : ToolDef<Edit> {
    static constexpr std::string_view tool_name = "edit";
    static constexpr std::string_view tool_desc =
        "Surgical text replacement. old_string must be unique in the file.";
    static constexpr auto params = std::tuple{
        Required<std::string>{"file_path", "Absolute path to the file"},
        Required<std::string>{"old_string", "Exact text to find (must be unique)"},
        Required<std::string>{"new_string", "Replacement text"},
    };
    static auto run(AnyExecutor& exec, std::string path,
                    std::string old_text, std::string new_text) -> Result<std::string>
    {
        return exec.edit_file(path, old_text, new_text);
    }
};

// ── Register all ─────────────────────────────────────────────────────────────

inline void register_all(ToolRegistry& registry) {
    registry.register_tool(Bash{});
    registry.register_tool(ReadFile{});
    registry.register_tool(WriteFile{});
    registry.register_tool(Glob{});
    registry.register_tool(Grep{});
    registry.register_tool(Edit{});
}

} // namespace clawed::tools
