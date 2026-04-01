#include <clawed/tui/terminal.hpp>

#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace clawed::tui {

Terminal::Terminal() {
    signal(SIGPIPE, SIG_IGN);
}

Terminal::~Terminal() {
    disable_raw();
}

void Terminal::enable_raw() {
    if (raw_active_) return;
    tcgetattr(STDIN_FILENO, &orig_);
    auto raw = orig_;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    raw_active_ = true;
    hide_cursor();
    flush();
}

void Terminal::disable_raw() {
    if (!raw_active_) return;
    flush();
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
    raw_active_ = false;
    show_cursor();
}

auto Terminal::width() const -> int {
    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col > 0 ? w.ws_col : 80;
}

auto Terminal::poll_input() -> InputEvent {
    struct pollfd pfd{};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 0) <= 0) return {};

    char buf[16];
    auto n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) return {};
    if (n > 1 && buf[0] == '\033') return {InputType::Escape};

    auto c = buf[0];
    if (c == 3 || c == 17) return {InputType::CtrlC};
    if (c == 21)           return {InputType::CtrlU};
    if (c == '\t')         return {InputType::Tab};
    if (c == '\r' || c == '\n') return {InputType::Enter};
    if (c == 127 || c == 8)    return {InputType::Backspace};
    if (c >= 32) return {InputType::Char, c};
    return {};
}

} // namespace clawed::tui
