#pragma once

#include <clawed/executor/concept.hpp>
#include <clawed/ssh/session.hpp>

#include <memory>

namespace clawed {

/// Executor that runs everything on a remote machine over SSH.
class SshExecutor {
public:
    explicit SshExecutor(std::shared_ptr<ssh::SshSession> session)
        : session_(std::move(session)) {}

    auto run_command(const std::string& cmd, std::chrono::seconds timeout) const
        -> Result<std::string>;

    auto read_file(const std::string& path, int offset, int limit) const
        -> Result<std::string>;

    auto write_file(const std::string& path, const std::string& content) const
        -> Result<std::string>;

    auto find_files(const std::string& pattern, const std::string& dir) const
        -> Result<std::string>;

    auto grep(const std::string& pattern, const std::string& path, int max_results) const
        -> Result<std::string>;

    auto edit_file(const std::string& path,
                   const std::string& old_text,
                   const std::string& new_text) const -> Result<std::string>;

private:
    std::shared_ptr<ssh::SshSession> session_;
};

} // namespace clawed
