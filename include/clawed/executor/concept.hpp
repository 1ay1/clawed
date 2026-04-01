#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>

#include <chrono>
#include <concepts>
#include <memory>
#include <string>

namespace clawed {

// ── Executor concept ─────────────────────────────────────────────────────────
// An Executor provides the I/O primitives that tools need.
// LocalExecutor runs on the host. SshExecutor runs over SSH.
// Tools are parameterized by executor — one implementation, any transport.

template <typename T>
concept Executor = requires(const T t,
                            const std::string& s,
                            int n,
                            std::chrono::seconds timeout) {
    // Run a shell command, return stdout/stderr. Non-zero exit → "Exit code: N\n..."
    { t.run_command(s, timeout) } -> std::same_as<Result<std::string>>;

    // Read file with line numbers. Returns formatted "  NNN\tline" output.
    { t.read_file(s, n, n) } -> std::same_as<Result<std::string>>;

    // Write content to file, creating parent dirs. Returns "Wrote N bytes to path".
    { t.write_file(s, s) } -> std::same_as<Result<std::string>>;

    // Find files matching glob pattern in directory. Returns newline-separated paths.
    { t.find_files(s, s) } -> std::same_as<Result<std::string>>;

    // Search file contents with regex. Returns matches with file:line:content.
    { t.grep(s, s, n) } -> std::same_as<Result<std::string>>;

    // Apply a surgical text replacement in a file.
    { t.edit_file(s, s, s) } -> std::same_as<Result<std::string>>;
};

// ── Type-erased executor ─────────────────────────────────────────────────────
// Wraps any Executor in a uniform heap-allocated container.

class AnyExecutor {
public:
    template <Executor E>
    explicit AnyExecutor(E executor)
        : impl_(std::make_unique<Model<E>>(std::move(executor))) {}

    AnyExecutor(AnyExecutor&&) noexcept = default;
    AnyExecutor& operator=(AnyExecutor&&) noexcept = default;

    auto run_command(const std::string& cmd, std::chrono::seconds timeout) const
        -> Result<std::string>
    {
        return impl_->run_command(cmd, timeout);
    }

    auto read_file(const std::string& path, int offset, int limit) const
        -> Result<std::string>
    {
        return impl_->read_file(path, offset, limit);
    }

    auto write_file(const std::string& path, const std::string& content) const
        -> Result<std::string>
    {
        return impl_->write_file(path, content);
    }

    auto find_files(const std::string& pattern, const std::string& dir) const
        -> Result<std::string>
    {
        return impl_->find_files(pattern, dir);
    }

    auto grep(const std::string& pattern, const std::string& path, int max_results) const
        -> Result<std::string>
    {
        return impl_->grep(pattern, path, max_results);
    }

    auto edit_file(const std::string& path,
                   const std::string& old_text,
                   const std::string& new_text) const -> Result<std::string>
    {
        return impl_->edit_file(path, old_text, new_text);
    }

private:
    struct Concept {
        virtual ~Concept() = default;
        virtual auto run_command(const std::string&, std::chrono::seconds) const
            -> Result<std::string> = 0;
        virtual auto read_file(const std::string&, int, int) const
            -> Result<std::string> = 0;
        virtual auto write_file(const std::string&, const std::string&) const
            -> Result<std::string> = 0;
        virtual auto find_files(const std::string&, const std::string&) const
            -> Result<std::string> = 0;
        virtual auto grep(const std::string&, const std::string&, int) const
            -> Result<std::string> = 0;
        virtual auto edit_file(const std::string&, const std::string&, const std::string&) const
            -> Result<std::string> = 0;
    };

    template <Executor E>
    struct Model final : Concept {
        E executor;
        explicit Model(E e) : executor(std::move(e)) {}

        auto run_command(const std::string& cmd, std::chrono::seconds t) const
            -> Result<std::string> override { return executor.run_command(cmd, t); }
        auto read_file(const std::string& p, int o, int l) const
            -> Result<std::string> override { return executor.read_file(p, o, l); }
        auto write_file(const std::string& p, const std::string& c) const
            -> Result<std::string> override { return executor.write_file(p, c); }
        auto find_files(const std::string& pat, const std::string& d) const
            -> Result<std::string> override { return executor.find_files(pat, d); }
        auto grep(const std::string& pat, const std::string& p, int m) const
            -> Result<std::string> override { return executor.grep(pat, p, m); }
        auto edit_file(const std::string& p, const std::string& o, const std::string& n) const
            -> Result<std::string> override { return executor.edit_file(p, o, n); }
    };

    std::unique_ptr<Concept> impl_;
};

} // namespace clawed
