#include <clawed/ide/terminal.hpp>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace clawed::ide {

// ── SIGWINCH handling ────────────────────────────────────────────────────────

static std::atomic<bool> g_resized{false};

static void sigwinch_handler(int) { g_resized.store(true); }

// ── Terminal ─────────────────────────────────────────────────────────────────

Terminal::Terminal() {
    tcgetattr(STDIN_FILENO, &saved_);
}

Terminal::~Terminal() {
    if (alt_screen_) leave_alt_screen();
    if (raw_) restore();
}

void Terminal::enable_raw_mode() {
    termios raw = saved_;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_ = true;

    // Install SIGWINCH handler
    struct sigaction sa{};
    sa.sa_handler = sigwinch_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);
}

void Terminal::restore() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_);
    raw_ = false;
}

void Terminal::enter_alt_screen() {
    write_seq("\033[?1049h");
    alt_screen_ = true;
}

void Terminal::leave_alt_screen() {
    write_seq("\033[?1049l");
    alt_screen_ = false;
}

void Terminal::hide_cursor() { write_seq("\033[?25l"); }
void Terminal::show_cursor() { write_seq("\033[?25h"); }

void Terminal::move_cursor(uint16_t x, uint16_t y) {
    char buf[32];
    auto n = snprintf(buf, sizeof(buf), "\033[%d;%dH", y + 1, x + 1);
    ::write(STDOUT_FILENO, buf, static_cast<size_t>(n));
}

auto Terminal::size() const -> std::pair<uint16_t, uint16_t> {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return {ws.ws_col, ws.ws_row};
    return {80, 24};
}

void Terminal::write_seq(const char* seq) {
    ::write(STDOUT_FILENO, seq, strlen(seq));
}

// ── Input parsing ────────────────────────────────────────────────────────────

auto Terminal::read_event(int timeout_ms) -> std::optional<InputEvent> {
    // Check resize flag first
    if (g_resized.exchange(false)) {
        auto [c, r] = size();
        return ResizeEvent{c, r};
    }

    pollfd pfd{STDIN_FILENO, POLLIN, 0};
    if (poll(&pfd, 1, timeout_ms) <= 0)
        return std::nullopt;

    unsigned char c = 0;
    if (::read(STDIN_FILENO, &c, 1) != 1)
        return std::nullopt;

    // Ctrl+key
    if (c < 32 && c != 27 && c != 13 && c != 9 && c != 127) {
        KeyEvent ke;
        ke.key = KeyEvent::Char;
        ke.ch = c + 64; // Ctrl+A = 1, maps to 'A'
        ke.ctrl = true;
        return ke;
    }

    // Escape
    if (c == 27)
        return parse_escape_seq();

    // Enter
    if (c == 13)
        return KeyEvent{KeyEvent::Enter};

    // Tab
    if (c == 9)
        return KeyEvent{KeyEvent::Tab};

    // Backspace
    if (c == 127)
        return KeyEvent{KeyEvent::Backspace};

    // Regular character (handle UTF-8)
    char32_t codepoint = c;
    if (c >= 0xC0) {
        // Multi-byte UTF-8
        int extra = (c < 0xE0) ? 1 : (c < 0xF0) ? 2 : 3;
        codepoint = c & (0x3F >> extra);
        for (int i = 0; i < extra; ++i) {
            unsigned char next = 0;
            if (::read(STDIN_FILENO, &next, 1) != 1) break;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
    }

    return KeyEvent{KeyEvent::Char, codepoint};
}

auto Terminal::parse_escape_seq() -> std::optional<InputEvent> {
    // Read next byte with short timeout
    pollfd pfd{STDIN_FILENO, POLLIN, 0};
    if (poll(&pfd, 1, 50) <= 0)
        return KeyEvent{KeyEvent::Escape}; // bare escape

    unsigned char seq[8]{};
    if (::read(STDIN_FILENO, &seq[0], 1) != 1)
        return KeyEvent{KeyEvent::Escape};

    if (seq[0] == '[') {
        // CSI sequence
        if (::read(STDIN_FILENO, &seq[1], 1) != 1)
            return KeyEvent{KeyEvent::Escape};

        // Check for modifier prefix (e.g., 1;5A for Ctrl+Up)
        if (seq[1] >= '0' && seq[1] <= '9') {
            // Read until we get a letter
            int i = 2;
            while (i < 7) {
                if (::read(STDIN_FILENO, &seq[i], 1) != 1) break;
                if (seq[i] >= '@') break;
                ++i;
            }

            // Parse modifier
            bool ctrl = false;
            // Check for ;5 modifier (ctrl)
            for (int j = 0; j < i; ++j) {
                if (seq[j] == ';' && j + 1 < i && seq[j+1] == '5') ctrl = true;
            }

            switch (seq[i]) {
                case 'A': return KeyEvent{KeyEvent::Up, 0, ctrl};
                case 'B': return KeyEvent{KeyEvent::Down, 0, ctrl};
                case 'C': return KeyEvent{KeyEvent::Right, 0, ctrl};
                case 'D': return KeyEvent{KeyEvent::Left, 0, ctrl};
                case '~':
                    if (seq[1] == '3') return KeyEvent{KeyEvent::Delete};
                    if (seq[1] == '5') return KeyEvent{KeyEvent::PageUp};
                    if (seq[1] == '6') return KeyEvent{KeyEvent::PageDown};
                    if (seq[1] == '1') return KeyEvent{KeyEvent::Home};
                    if (seq[1] == '4') return KeyEvent{KeyEvent::End};
                    break;
            }
            return std::nullopt;
        }

        switch (seq[1]) {
            case 'A': return KeyEvent{KeyEvent::Up};
            case 'B': return KeyEvent{KeyEvent::Down};
            case 'C': return KeyEvent{KeyEvent::Right};
            case 'D': return KeyEvent{KeyEvent::Left};
            case 'H': return KeyEvent{KeyEvent::Home};
            case 'F': return KeyEvent{KeyEvent::End};
        }
    } else if (seq[0] == 'O') {
        // SS3 sequence (legacy arrow keys)
        if (::read(STDIN_FILENO, &seq[1], 1) != 1)
            return KeyEvent{KeyEvent::Escape};
        switch (seq[1]) {
            case 'A': return KeyEvent{KeyEvent::Up};
            case 'B': return KeyEvent{KeyEvent::Down};
            case 'C': return KeyEvent{KeyEvent::Right};
            case 'D': return KeyEvent{KeyEvent::Left};
            case 'H': return KeyEvent{KeyEvent::Home};
            case 'F': return KeyEvent{KeyEvent::End};
        }
    } else {
        // Alt+key
        KeyEvent ke;
        ke.key = KeyEvent::Char;
        ke.ch = seq[0];
        ke.alt = true;
        return ke;
    }

    return std::nullopt;
}

} // namespace clawed::ide
