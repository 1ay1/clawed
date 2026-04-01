#include <clawed/executor/ssh.hpp>
#include <clawed/ssh/session.hpp>

#include <format>
#include <string_view>

namespace clawed {

// ── run_command ──────────────────────────────────────────────────────────────

auto SshExecutor::run_command(const std::string& cmd,
                              std::chrono::seconds timeout) const
    -> Result<std::string>
{
    return session_->run(cmd, timeout);
}

// ── read_file ────────────────────────────────────────────────────────────────

auto SshExecutor::read_file(const std::string& path, int offset, int limit) const
    -> Result<std::string>
{
    if (offset < 1) offset = 1;
    if (limit < 1)  limit = 2000;
    int end = offset + limit;

    // If it's a directory, auto-list contents
    auto type_check = session_->run(
        "test -d " + ssh::shell_escape(path) + " && echo __DIR__ || test -f " +
        ssh::shell_escape(path) + " && echo __FILE__",
        std::chrono::seconds{5});

    if (type_check) {
        auto& tc = *type_check;
        if (tc.find("__DIR__") != std::string::npos)
            return session_->run("ls -la " + ssh::shell_escape(path), std::chrono::seconds{5});
    }

    auto cmd = std::format(
        "test -f {} && awk 'NR >= {} && NR < {} {{ printf \"%6d\\t%s\\n\", NR, $0 }}' {}",
        ssh::shell_escape(path), offset, end, ssh::shell_escape(path));

    auto result = session_->run(cmd, std::chrono::seconds{30});
    if (!result) return std::unexpected(result.error());

    auto& output = *result;
    if (output.starts_with("Exit code: 1"))
        return make_error(ErrorCode::ToolExecFailed, std::format("file not found: {}", path));

    if (output.empty()) return std::string("(empty file or offset beyond file end)");
    return output;
}

// ── write_file ───────────────────────────────────────────────────────────────

auto SshExecutor::write_file(const std::string& path, const std::string& content) const
    -> Result<std::string>
{
    auto write_result = session_->write_remote_file(path, content);
    if (!write_result) return std::unexpected(write_result.error());

    auto stat_result = session_->run(
        "stat --format='%s' " + ssh::shell_escape(path), std::chrono::seconds{5});

    std::string size_str = stat_result ? *stat_result : std::to_string(content.size());
    while (!size_str.empty() && (size_str.back() == '\n' || size_str.back() == '\r'))
        size_str.pop_back();

    return std::format("Wrote {} bytes to {}", size_str, path);
}

// ── find_files ───────────────────────────────────────────────────────────────

namespace {

struct GlobParts {
    std::string base_dir;
    std::string name_pattern;
    bool recursive;
};

auto decompose_pattern(const std::string& pattern) -> GlobParts {
    GlobParts parts;
    parts.recursive = pattern.find("**") != std::string::npos;

    auto last_slash = pattern.rfind('/');
    if (last_slash == std::string::npos) {
        parts.name_pattern = pattern;
        if (parts.name_pattern.starts_with("**/"))
            parts.name_pattern = parts.name_pattern.substr(3);
        else if (parts.name_pattern == "**")
            parts.name_pattern = "*";
    } else {
        parts.name_pattern = pattern.substr(last_slash + 1);
        auto dir_part = pattern.substr(0, last_slash);
        if (dir_part == "**") {
            // base_dir stays empty
        } else {
            auto ds = dir_part.find("/**");
            if (ds != std::string::npos)
                parts.base_dir = dir_part.substr(0, ds);
            else {
                ds = dir_part.find("**");
                if (ds != std::string::npos)
                    parts.base_dir = dir_part.substr(0, ds);
                else
                    parts.base_dir = dir_part;
            }
        }
    }

    if (parts.name_pattern.empty()) parts.name_pattern = "*";
    return parts;
}

} // anonymous namespace

auto SshExecutor::find_files(const std::string& pattern, const std::string& dir) const
    -> Result<std::string>
{
    auto parts = decompose_pattern(pattern);

    std::string root;
    if (!dir.empty()) {
        root = dir;
        if (!parts.base_dir.empty()) root += "/" + parts.base_dir;
    } else if (!parts.base_dir.empty()) {
        root = parts.base_dir;
    } else {
        root = ".";
    }

    std::string cmd;
    if (parts.recursive) {
        cmd = std::format("find {} -type f -name {} 2>/dev/null | sort | head -501",
            ssh::shell_escape(root), ssh::shell_escape(parts.name_pattern));
    } else {
        cmd = std::format("find {} -maxdepth 1 -type f -name {} 2>/dev/null | sort | head -501",
            ssh::shell_escape(root), ssh::shell_escape(parts.name_pattern));
    }

    auto result = session_->run(cmd, std::chrono::seconds{30});
    if (!result) return std::unexpected(result.error());

    auto& output = *result;
    if (output.empty()) return std::string("No matches found.");

    // Trim to 500 if we got 501
    int line_count = 0;
    for (char c : output) if (c == '\n') ++line_count;

    if (line_count > 500) {
        std::string trimmed;
        int count = 0;
        for (size_t i = 0; i < output.size(); ++i) {
            trimmed += output[i];
            if (output[i] == '\n' && ++count >= 500) {
                trimmed += "... and more results (showing first 500)\n";
                return trimmed;
            }
        }
    }

    return output;
}

// ── grep ─────────────────────────────────────────────────────────────────────

auto SshExecutor::grep(const std::string& pattern, const std::string& path,
                       int max_results) const -> Result<std::string>
{
    std::string search_path = path.empty() ? "." : path;
    if (max_results <= 0) max_results = 200;

    auto cmd = std::format(
        "{{ command -v rg > /dev/null && rg -n --no-heading --max-count {} {} {}; }} 2>/dev/null || "
        "grep -rn {} {} 2>/dev/null | head -{}",
        max_results, ssh::shell_escape(pattern), ssh::shell_escape(search_path),
        ssh::shell_escape(pattern), ssh::shell_escape(search_path), max_results);

    return session_->run(cmd, std::chrono::seconds{30});
}

// ── edit_file ────────────────────────────────────────────────────────────────

auto SshExecutor::edit_file(const std::string& path,
                            const std::string& old_text,
                            const std::string& new_text) const -> Result<std::string>
{
    // Read the file
    auto read_result = session_->run("cat " + ssh::shell_escape(path), std::chrono::seconds{10});
    if (!read_result) return std::unexpected(read_result.error());

    auto& content = *read_result;
    if (content.starts_with("Exit code:"))
        return make_error(ErrorCode::ToolExecFailed, std::format("file not found: {}", path));

    auto pos = content.find(old_text);
    if (pos == std::string::npos)
        return make_error(ErrorCode::ToolExecFailed, "old_string not found in file");

    if (content.find(old_text, pos + old_text.size()) != std::string::npos)
        return make_error(ErrorCode::ToolExecFailed,
            "old_string is not unique in file — provide more context");

    content.replace(pos, old_text.size(), new_text);

    // Write back
    auto write_result = session_->write_remote_file(path, content);
    if (!write_result) return std::unexpected(write_result.error());

    int line = 1;
    for (size_t i = 0; i < pos; ++i) {
        if (content[i] == '\n') ++line;
    }

    auto old_lines = std::count(old_text.begin(), old_text.end(), '\n') + 1;
    auto new_lines = std::count(new_text.begin(), new_text.end(), '\n') + 1;

    return std::format("Edited {} at line {} ({} → {} lines)", path, line, old_lines, new_lines);
}

} // namespace clawed
