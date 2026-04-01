#pragma once

#include <clawed/executor/concept.hpp>
#include <filesystem>

namespace clawed {

/// Executor that runs everything on the local machine.
class LocalExecutor {
public:
    explicit LocalExecutor(std::filesystem::path working_dir = std::filesystem::current_path())
        : working_dir_(std::move(working_dir)) {}

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
    std::filesystem::path working_dir_;
};

} // namespace clawed
