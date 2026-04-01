#pragma once

// ── Terminal ─────────────────────────────────────────────────────────────────
// Blazing fast terminal I/O. Key optimizations:
//
// 1. WRITE BUFFER — all output accumulates in a 8KB stack buffer. One write()
//    syscall per flush(). Typical frame: 50+ term.write() calls → 1 syscall.
//
// 2. DIRECT SYSCALLS — no stdio, no iostream, no buffering layers.
//    write(STDOUT_FILENO, ...) and read(STDIN_FILENO, ...) only.
//
// 3. NON-BLOCKING INPUT — poll() with zero timeout. Never blocks.
//
// Threading: Terminal is called from both the agent thread (token writes)
// and the UI thread (tools, spinner, input). Callers must hold a mutex.
// Terminal itself is not thread-safe — the mutex is external (State::mu).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <termios.h>
#include <unistd.h>

namespace clawed::tui {

enum class InputType : uint8_t {
    None, Char, Enter, Backspace, CtrlC, CtrlU, Tab, Escape
};

struct InputEvent {
    InputType type = InputType::None;
    char ch = 0;
};

class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    void enable_raw();
    void disable_raw();

    // ── Buffered output (hot path) ──────────────────────────────────────────
    // All write() calls copy into buf_. Call flush() to emit one syscall.

    void write(std::string_view s) {
        auto len = s.size();
        auto* src = s.data();

        // Fast path: fits in buffer
        if (buf_len_ + len <= BUF_SIZE) [[likely]] {
            __builtin_memcpy(buf_ + buf_len_, src, len);
            buf_len_ += len;
            return;
        }

        // Doesn't fit — flush current buffer, then handle
        flush();
        if (len <= BUF_SIZE) [[likely]] {
            __builtin_memcpy(buf_, src, len);
            buf_len_ = len;
        } else {
            // Huge write (>8KB) — bypass buffer entirely
            ::write(STDOUT_FILENO, src, len);
        }
    }

    void write(char c) {
        if (buf_len_ >= BUF_SIZE) [[unlikely]] flush();
        buf_[buf_len_++] = c;
    }

    void writeln(std::string_view s = "") {
        if (!s.empty()) write(s);
        write('\n');
    }

    /// Flush write buffer to terminal. One syscall.
    void flush() {
        if (buf_len_ > 0) [[likely]] {
            ::write(STDOUT_FILENO, buf_, buf_len_);
            buf_len_ = 0;
        }
    }

    // ── Cursor & screen (auto-flush for immediate effect) ───────────────────

    void hide_cursor()  { write("\033[?25l"); }
    void show_cursor()  { write("\033[?25h"); flush(); }
    void clear_line()   { write("\033[2K\r"); }
    void move_up(int n = 1) {
        for (int i = 0; i < n; ++i) write("\033[A");
    }
    void clear_to_end() { write("\033[J"); }

    void clear_lines_above(int n) {
        for (int i = 0; i < n; ++i) { clear_line(); move_up(); }
        clear_line();
    }

    [[nodiscard]] auto width() const -> int;
    [[nodiscard]] auto poll_input() -> InputEvent;

private:
    static constexpr size_t BUF_SIZE = 8192;
    char buf_[BUF_SIZE];
    size_t buf_len_ = 0;

    struct termios orig_{};
    bool raw_active_ = false;
};

} // namespace clawed::tui
