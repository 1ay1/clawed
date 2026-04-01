#include <clawed/executor/local.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <fnmatch.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace clawed {

// ── run_command ──────────────────────────────────────────────────────────────

auto LocalExecutor::run_command(const std::string& cmd,
                                std::chrono::seconds timeout) const
    -> Result<std::string>
{
    int pipefd[2];
    if (pipe(pipefd) == -1)
        return make_error(ErrorCode::ToolExecFailed, "pipe() failed");

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]); close(pipefd[1]);
        return make_error(ErrorCode::ToolExecFailed, "fork() failed");
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (chdir(working_dir_.c_str()) != 0) _exit(127);
        execl("/bin/bash", "bash", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    close(pipefd[1]);

    std::string output;
    std::array<char, 4096> buf{};
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        if (std::chrono::steady_clock::now() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            close(pipefd[0]);
            return make_error(ErrorCode::ToolTimeout,
                std::format("command timed out after {}s", timeout.count()));
        }

        auto n = read(pipefd[0], buf.data(), buf.size());
        if (n <= 0) break;
        output.append(buf.data(), static_cast<size_t>(n));

        if (output.size() > 1'000'000) {
            output += "\n... [output truncated at 1MB]";
            break;
        }
    }

    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    if (exit_code != 0) {
        return std::format("Exit code: {}\n{}", exit_code, output);
    }

    return output;
}

// ── read_file ────────────────────────────────────────────────────────────────

auto LocalExecutor::read_file(const std::string& path, int offset, int limit) const
    -> Result<std::string>
{
    if (!std::filesystem::exists(path))
        return make_error(ErrorCode::ToolExecFailed, std::format("file not found: {}", path));
    if (std::filesystem::is_directory(path)) {
        // Auto-list directory contents instead of erroring
        return run_command("ls -la " + path, std::chrono::seconds{5});
    }
    if (!std::filesystem::is_regular_file(path))
        return make_error(ErrorCode::ToolExecFailed, std::format("not a regular file: {}", path));

    std::ifstream file(path);
    if (!file.is_open())
        return make_error(ErrorCode::ToolExecFailed, std::format("cannot open file: {}", path));

    if (offset < 1) offset = 1;
    if (limit < 1)  limit  = 2000;

    std::string result;
    std::string line;
    int line_num = 0, lines_read = 0;

    while (std::getline(file, line)) {
        ++line_num;
        if (line_num < offset) continue;
        if (lines_read >= limit) break;
        result += std::format("{:6d}\t{}\n", line_num, line);
        ++lines_read;
    }

    if (result.empty()) return std::string("(empty file or offset beyond file end)");
    return result;
}

// ── write_file ───────────────────────────────────────────────────────────────

auto LocalExecutor::write_file(const std::string& path, const std::string& content) const
    -> Result<std::string>
{
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent))
        std::filesystem::create_directories(parent);

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open())
        return make_error(ErrorCode::ToolExecFailed, std::format("cannot write to: {}", path));

    file << content;
    file.close();

    auto size = std::filesystem::file_size(path);
    return std::format("Wrote {} bytes to {}", size, path);
}

// ── find_files ───────────────────────────────────────────────────────────────

auto LocalExecutor::find_files(const std::string& pattern, const std::string& dir) const
    -> Result<std::string>
{
    std::filesystem::path root = dir.empty() ? working_dir_ : std::filesystem::path(dir);

    if (!std::filesystem::exists(root))
        return make_error(ErrorCode::ToolExecFailed,
            std::format("directory not found: {}", root.string()));

    bool recursive = pattern.find("**") != std::string::npos;
    auto opts = std::filesystem::directory_options::skip_permission_denied;

    std::vector<std::string> matches;
    if (recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, opts)) {
            auto rel = std::filesystem::relative(entry.path(), root).string();
            if (fnmatch(pattern.c_str(), rel.c_str(), FNM_PATHNAME) == 0)
                matches.push_back(entry.path().string());
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(root, opts)) {
            auto name = entry.path().filename().string();
            if (fnmatch(pattern.c_str(), name.c_str(), 0) == 0)
                matches.push_back(entry.path().string());
        }
    }

    std::sort(matches.begin(), matches.end());

    if (matches.empty()) return std::string("No matches found.");

    std::string result;
    auto limit = std::min(matches.size(), size_t{500});
    for (size_t i = 0; i < limit; ++i) {
        result += matches[i];
        result += '\n';
    }
    if (matches.size() > 500)
        result += std::format("... and {} more\n", matches.size() - 500);

    return result;
}

// ── grep ─────────────────────────────────────────────────────────────────────

auto LocalExecutor::grep(const std::string& pattern, const std::string& path,
                         int max_results) const -> Result<std::string>
{
    // Use grep -rn for recursive search with line numbers.
    // Try rg first (faster), fall back to grep.
    std::string search_path = path.empty() ? working_dir_.string() : path;
    if (max_results <= 0) max_results = 200;

    auto cmd = std::format(
        "{{ command -v rg > /dev/null && rg -n --no-heading --max-count {} '{}' {}; }} 2>/dev/null || "
        "grep -rn --include='*' '{}' {} 2>/dev/null | head -{}",
        max_results, pattern, search_path,
        pattern, search_path, max_results);

    return run_command(cmd, std::chrono::seconds{30});
}

// ── edit_file ────────────────────────────────────────────────────────────────

auto LocalExecutor::edit_file(const std::string& path,
                              const std::string& old_text,
                              const std::string& new_text) const -> Result<std::string>
{
    if (!std::filesystem::exists(path))
        return make_error(ErrorCode::ToolExecFailed, std::format("file not found: {}", path));

    std::ifstream in(path);
    if (!in.is_open())
        return make_error(ErrorCode::ToolExecFailed, std::format("cannot open file: {}", path));

    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    auto pos = content.find(old_text);
    if (pos == std::string::npos)
        return make_error(ErrorCode::ToolExecFailed, "old_string not found in file");

    // Ensure uniqueness — old_text must appear exactly once
    if (content.find(old_text, pos + old_text.size()) != std::string::npos)
        return make_error(ErrorCode::ToolExecFailed,
            "old_string is not unique in file — provide more context");

    content.replace(pos, old_text.size(), new_text);

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open())
        return make_error(ErrorCode::ToolExecFailed, std::format("cannot write to: {}", path));

    out << content;
    out.close();

    // Calculate line number of the edit
    int line = 1;
    for (size_t i = 0; i < pos; ++i) {
        if (content[i] == '\n') ++line;
    }

    auto old_lines = std::count(old_text.begin(), old_text.end(), '\n') + 1;
    auto new_lines = std::count(new_text.begin(), new_text.end(), '\n') + 1;

    return std::format("Edited {} at line {} ({} → {} lines)", path, line, old_lines, new_lines);
}

} // namespace clawed
