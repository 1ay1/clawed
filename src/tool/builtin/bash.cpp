#include <clawed/tool/builtin/bash.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace clawed::builtin {

auto BashTool::execute(const nlohmann::json& input) const -> Result<std::string> {
    if (!input.contains("command") || !input["command"].is_string()) {
        return make_error(ErrorCode::ToolExecFailed, "missing 'command' parameter");
    }

    auto command = input["command"].get<std::string>();
    auto timeout = default_timeout;
    if (input.contains("timeout") && input["timeout"].is_number_integer()) {
        timeout = std::chrono::seconds(input["timeout"].get<int>());
    }

    // Create pipe for capturing output.
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return make_error(ErrorCode::ToolExecFailed, "pipe() failed");
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return make_error(ErrorCode::ToolExecFailed, "fork() failed");
    }

    if (pid == 0) {
        // Child process.
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Change to working directory.
        if (chdir(working_dir.c_str()) != 0) {
            _exit(127);
        }

        execl("/bin/bash", "bash", "-c", command.c_str(), nullptr);
        _exit(127);
    }

    // Parent process.
    close(pipefd[1]);

    // Read output with timeout awareness.
    std::string output;
    std::array<char, 4096> buf{};
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        // Check timeout.
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

        // Cap output size at 1MB.
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

} // namespace clawed::builtin
