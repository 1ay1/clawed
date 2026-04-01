#include <clawed/ssh/session.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace clawed::ssh {

auto shell_escape(const std::string& s) -> std::string {
    std::string escaped = "'";
    for (char c : s) {
        if (c == '\'') escaped += "'\\''";
        else escaped += c;
    }
    escaped += "'";
    return escaped;
}

// ---------------------------------------------------------------------------
// Construction / destruction / move
// ---------------------------------------------------------------------------

SshSession::SshSession(Config config)
    : config_(std::move(config)) {}

SshSession::~SshSession() {
    if (connected_) disconnect();
}

SshSession::SshSession(SshSession&& other) noexcept
    : config_(std::move(other.config_))
    , control_path_(std::move(other.control_path_))
    , control_dir_(std::move(other.control_dir_))
    , connected_(other.connected_)
{
    other.connected_ = false;
}

SshSession& SshSession::operator=(SshSession&& other) noexcept {
    if (this != &other) {
        if (connected_) disconnect();
        config_       = std::move(other.config_);
        control_path_ = std::move(other.control_path_);
        control_dir_  = std::move(other.control_dir_);
        connected_    = other.connected_;
        other.connected_ = false;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// SSH arg helpers
// ---------------------------------------------------------------------------

auto SshSession::base_ssh_args() const -> std::vector<std::string> {
    return {
        "ssh",
        "-o", "ControlMaster=no",
        "-o", std::format("ControlPath={}", control_path_.string()),
        "-o", "BatchMode=yes",
        config_.host,
    };
}

// ---------------------------------------------------------------------------
// exec_ssh — fork/exec with optional stdin, timeout, output capture
// ---------------------------------------------------------------------------

auto SshSession::exec_ssh(const std::vector<std::string>& args,
                          const std::string* stdin_data,
                          std::chrono::seconds timeout)
    -> Result<std::pair<int, std::string>>
{
    // stdout/stderr pipe
    int out_pipe[2];
    if (pipe(out_pipe) == -1) {
        return make_error(ErrorCode::ToolExecFailed, "pipe() failed");
    }

    // stdin pipe (only if we have data to send)
    int in_pipe[2] = {-1, -1};
    if (stdin_data) {
        if (pipe(in_pipe) == -1) {
            close(out_pipe[0]); close(out_pipe[1]);
            return make_error(ErrorCode::ToolExecFailed, "pipe() failed");
        }
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(out_pipe[0]); close(out_pipe[1]);
        if (stdin_data) { close(in_pipe[0]); close(in_pipe[1]); }
        return make_error(ErrorCode::ToolExecFailed, "fork() failed");
    }

    if (pid == 0) {
        // Child — set up pipes and exec.
        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);

        if (stdin_data) {
            close(in_pipe[1]);
            dup2(in_pipe[0], STDIN_FILENO);
            close(in_pipe[0]);
        } else {
            // No stdin — redirect /dev/null
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
        }

        // Build argv
        std::vector<const char*> argv;
        argv.reserve(args.size() + 1);
        for (auto& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);

        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    // Parent
    close(out_pipe[1]);
    if (stdin_data) {
        close(in_pipe[0]);
        // Write stdin data to child. Ignore partial writes for simplicity.
        auto remaining = stdin_data->size();
        auto ptr = stdin_data->data();
        while (remaining > 0) {
            auto n = write(in_pipe[1], ptr, remaining);
            if (n <= 0) break;
            ptr += n;
            remaining -= static_cast<size_t>(n);
        }
        close(in_pipe[1]);
    }

    // Read output with timeout
    std::string output;
    std::array<char, 4096> buf{};
    auto deadline = std::chrono::steady_clock::now() + timeout;

    // Set non-blocking on read end
    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);

    while (true) {
        if (std::chrono::steady_clock::now() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            close(out_pipe[0]);
            return make_error(ErrorCode::ToolTimeout,
                std::format("SSH command timed out after {}s", timeout.count()));
        }

        auto n = read(out_pipe[0], buf.data(), buf.size());
        if (n > 0) {
            output.append(buf.data(), static_cast<size_t>(n));
            if (output.size() > 1'000'000) {
                output += "\n... [output truncated at 1MB]";
                break;
            }
        } else if (n == 0) {
            break; // EOF
        } else {
            // EAGAIN — no data yet, brief sleep
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            break; // real error
        }
    }

    close(out_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    return std::pair{exit_code, std::move(output)};
}

// ---------------------------------------------------------------------------
// connect
// ---------------------------------------------------------------------------

auto SshSession::connect() -> Result<void> {
    // Create temp directory for control socket
    char tmpl[] = "/tmp/clawed-ssh-XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) {
        return make_error(ErrorCode::SshConnectionFailed,
            "failed to create temp directory for SSH control socket");
    }
    control_dir_  = dir;
    control_path_ = control_dir_ / "ctrl";

    // Launch ControlMaster in background
    pid_t pid = fork();
    if (pid == -1) {
        return make_error(ErrorCode::SshConnectionFailed, "fork() failed");
    }

    if (pid == 0) {
        // Child — start ControlMaster
        // Detach stdin/stdout/stderr
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execlp("ssh", "ssh",
            "-o", "ControlMaster=yes",
            "-o", std::format("ControlPath={}", control_path_.string()).c_str(),
            "-o", "ControlPersist=yes",
            "-o", "ServerAliveInterval=15",
            "-o", "ServerAliveCountMax=3",
            "-o", "ConnectTimeout=10",
            "-N",
            config_.host.c_str(),
            nullptr);
        _exit(127);
    }

    // Wait for the control socket to appear (up to 15s)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    bool socket_appeared = false;

    while (std::chrono::steady_clock::now() < deadline) {
        struct stat st;
        if (stat(control_path_.c_str(), &st) == 0) {
            socket_appeared = true;
            break;
        }

        // Check if child died
        int status = 0;
        auto w = waitpid(pid, &status, WNOHANG);
        if (w > 0 && (WIFEXITED(status) || WIFSIGNALED(status))) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!socket_appeared) {
        // Kill the child if still running
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        std::filesystem::remove_all(control_dir_);
        return make_error(ErrorCode::SshConnectionFailed,
            std::format("SSH connection to {} timed out", config_.host));
    }

    // Verify with -O check
    auto check_result = exec_ssh(
        {"ssh",
         "-o", std::format("ControlPath={}", control_path_.string()),
         "-O", "check",
         config_.host},
        nullptr,
        std::chrono::seconds{5});

    if (!check_result || check_result->first != 0) {
        std::filesystem::remove_all(control_dir_);
        return make_error(ErrorCode::SshConnectionFailed,
            std::format("SSH connection check failed for {}", config_.host));
    }

    connected_ = true;
    return {};
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------

auto SshSession::run(const std::string& command, std::chrono::seconds timeout)
    -> Result<std::string>
{
    if (!connected_) {
        return make_error(ErrorCode::SshConnectionFailed, "not connected");
    }

    // Build the remote command: cd to working dir, then execute
    std::string remote_cmd;
    if (!config_.remote_dir.empty()) {
        remote_cmd = "cd " + shell_escape(config_.remote_dir) + " && ";
    }
    remote_cmd += "bash -c " + shell_escape(command);

    auto args = base_ssh_args();
    args.push_back(remote_cmd);

    auto result = exec_ssh(args, nullptr, timeout);
    if (!result) return std::unexpected(result.error());

    auto& [exit_code, output] = *result;

    // Exit code 255 = SSH transport failure
    if (exit_code == 255) {
        return make_error(ErrorCode::SshConnectionFailed,
            std::format("SSH connection lost to {}: {}", config_.host, output));
    }

    if (exit_code != 0) {
        return std::format("Exit code: {}\n{}", exit_code, output);
    }

    return output;
}

// ---------------------------------------------------------------------------
// write_remote_file
// ---------------------------------------------------------------------------

auto SshSession::write_remote_file(const std::string& path,
                                   const std::string& content) -> Result<void>
{
    if (!connected_) {
        return make_error(ErrorCode::SshConnectionFailed, "not connected");
    }

    // Create parent directory
    auto parent = std::filesystem::path(path).parent_path().string();
    if (!parent.empty()) {
        auto mkdir_result = run("mkdir -p " + shell_escape(parent), std::chrono::seconds{10});
        if (!mkdir_result) return std::unexpected(mkdir_result.error());
    }

    // Pipe content to cat > path
    auto args = base_ssh_args();
    args.push_back("cat > " + shell_escape(path));

    auto result = exec_ssh(args, &content, std::chrono::seconds{30});
    if (!result) return std::unexpected(result.error());

    if (result->first != 0) {
        return make_error(ErrorCode::ToolExecFailed,
            std::format("failed to write {}: {}", path, result->second));
    }

    return {};
}

// ---------------------------------------------------------------------------
// disconnect
// ---------------------------------------------------------------------------

void SshSession::disconnect() {
    if (!connected_) return;
    connected_ = false;

    // Send exit command to ControlMaster
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execlp("ssh", "ssh",
            "-o", std::format("ControlPath={}", control_path_.string()).c_str(),
            "-O", "exit",
            config_.host.c_str(),
            nullptr);
        _exit(127);
    }
    if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }

    // Cleanup temp directory
    std::error_code ec;
    std::filesystem::remove_all(control_dir_, ec);
}

} // namespace clawed::ssh
