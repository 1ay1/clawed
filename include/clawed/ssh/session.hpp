#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace clawed::ssh {

/// Shell-escapes a string using POSIX single-quote wrapping.
auto shell_escape(const std::string& s) -> std::string;

class SshSession {
public:
    struct Config {
        std::string host;        // "user@host" or just "host"
        std::string remote_dir;  // working directory on remote
    };

    explicit SshSession(Config config);
    ~SshSession();

    SshSession(const SshSession&) = delete;
    SshSession& operator=(const SshSession&) = delete;
    SshSession(SshSession&&) noexcept;
    SshSession& operator=(SshSession&&) noexcept;

    /// Establish ControlMaster connection.
    auto connect() -> Result<void>;

    /// Run a command on the remote host (in remote_dir).
    auto run(const std::string& command,
             std::chrono::seconds timeout = std::chrono::seconds{120})
        -> Result<std::string>;

    /// Write content to a file on the remote host via stdin pipe.
    auto write_remote_file(const std::string& path,
                           const std::string& content) -> Result<void>;

    /// Tear down ControlMaster.
    void disconnect();

    [[nodiscard]] auto host() const -> const std::string& { return config_.host; }
    [[nodiscard]] auto remote_dir() const -> const std::string& { return config_.remote_dir; }
    void set_remote_dir(const std::string& dir) { config_.remote_dir = dir; }

private:
    Config config_;
    std::filesystem::path control_path_;
    std::filesystem::path control_dir_;
    bool connected_ = false;

    auto base_ssh_args() const -> std::vector<std::string>;

    /// Fork/exec an ssh command, optionally pipe stdin, capture output.
    auto exec_ssh(const std::vector<std::string>& args,
                  const std::string* stdin_data,
                  std::chrono::seconds timeout)
        -> Result<std::pair<int, std::string>>;
};

} // namespace clawed::ssh
