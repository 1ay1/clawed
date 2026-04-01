#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <termios.h>

namespace clawed::ide {

// ── Input events ─────────────────────────────────────────────────────────────

struct KeyEvent {
    enum Key : uint16_t {
        Char, Escape, Enter, Tab, Backspace, Delete,
        Up, Down, Left, Right, Home, End, PageUp, PageDown,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    };
    Key      key  = Char;
    char32_t ch   = 0;
    bool     ctrl = false;
    bool     alt  = false;
};

struct ResizeEvent { uint16_t cols; uint16_t rows; };

using InputEvent = std::variant<KeyEvent, ResizeEvent>;

// ── Terminal ─────────────────────────────────────────────────────────────────

class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    void enable_raw_mode();
    void restore();

    void enter_alt_screen();
    void leave_alt_screen();
    void hide_cursor();
    void show_cursor();
    void move_cursor(uint16_t x, uint16_t y);

    auto size() const -> std::pair<uint16_t, uint16_t>;
    auto read_event(int timeout_ms = 16) -> std::optional<InputEvent>;

private:
    termios saved_;
    bool    raw_ = false;
    bool    alt_screen_ = false;

    auto parse_escape_seq() -> std::optional<InputEvent>;
    void write_seq(const char* seq);
};

} // namespace clawed::ide
