#include <clawed/ide/widgets/status_bar.hpp>
#include <format>

namespace clawed::ide::widgets {

void StatusBar::render(Rect area, Buffer& buf) {
    dirty_ = false;
    if (area.height == 0) return;

    // Inverted style (white on dark)
    Style bar_style = Style{}.with_fg(Color::Black).with_bg(Color::White);
    buf.fill(area, ' ', bar_style);

    // Left: file path
    if (!file_.empty()) {
        auto display = file_;
        if (display.size() > area.width / 2)
            display = "..." + display.substr(display.size() - area.width / 2 + 3);
        buf.put_str(area.x + 1, area.y, display, bar_style);
    }

    // Center: line:col
    if (line_ > 0) {
        auto pos = std::format("{}:{}", line_, col_);
        auto center_x = area.x + area.width / 2 - static_cast<uint16_t>(pos.size() / 2);
        buf.put_str(center_x, area.y, pos, bar_style);
    }

    // Right: agent status + version
    auto right = std::format("{} | clawed", status_);
    if (right.size() + 2 < area.width) {
        auto rx = area.x + area.width - static_cast<uint16_t>(right.size()) - 1;
        buf.put_str(rx, area.y, right, bar_style);
    }
}

} // namespace clawed::ide::widgets
