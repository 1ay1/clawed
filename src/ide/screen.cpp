#include <clawed/ide/screen.hpp>
#include <clawed/tui/style.hpp>

#include <cstdio>
#include <unistd.h>

namespace clawed::ide {

// ── Buffer ───────────────────────────────────────────────────────────────────

static const Cell EMPTY_CELL{' ', {}};

Buffer::Buffer(uint16_t cols, uint16_t rows)
    : cols_(cols), rows_(rows), cells_(static_cast<size_t>(cols) * rows, EMPTY_CELL) {}

auto Buffer::get(uint16_t x, uint16_t y) const -> const Cell& {
    if (!in_bounds(x, y)) return EMPTY_CELL;
    return cells_[idx(x, y)];
}

void Buffer::set(uint16_t x, uint16_t y, Cell cell) {
    if (in_bounds(x, y)) cells_[idx(x, y)] = cell;
}

auto Buffer::put_str(uint16_t x, uint16_t y, std::string_view text, Style style) -> uint16_t {
    uint16_t col = x;
    size_t i = 0;
    while (i < text.size() && col < cols_) {
        auto c = static_cast<unsigned char>(text[i]);
        char32_t cp;
        if (c < 0x80) {
            cp = c;
            ++i;
        } else if (c < 0xE0) {
            cp = c & 0x1F;
            if (i + 1 < text.size()) cp = (cp << 6) | (text[i+1] & 0x3F);
            i += 2;
        } else if (c < 0xF0) {
            cp = c & 0x0F;
            if (i + 2 < text.size()) {
                cp = (cp << 6) | (text[i+1] & 0x3F);
                cp = (cp << 6) | (text[i+2] & 0x3F);
            }
            i += 3;
        } else {
            cp = c & 0x07;
            if (i + 3 < text.size()) {
                cp = (cp << 6) | (text[i+1] & 0x3F);
                cp = (cp << 6) | (text[i+2] & 0x3F);
                cp = (cp << 6) | (text[i+3] & 0x3F);
            }
            i += 4;
        }

        if (in_bounds(col, y))
            cells_[idx(col, y)] = Cell{cp, style};
        ++col;
    }
    return col - x;
}

void Buffer::fill(Rect area, Cell cell) {
    for (uint16_t row = area.y; row < area.y + area.height && row < rows_; ++row)
        for (uint16_t col = area.x; col < area.x + area.width && col < cols_; ++col)
            cells_[idx(col, row)] = cell;
}

void Buffer::fill(Rect area, char32_t ch, Style style) {
    fill(area, Cell{ch, style});
}

void Buffer::hline(uint16_t x, uint16_t y, uint16_t len, char32_t ch, Style style) {
    for (uint16_t i = 0; i < len && x + i < cols_; ++i)
        set(x + i, y, Cell{ch, style});
}

void Buffer::vline(uint16_t x, uint16_t y, uint16_t len, char32_t ch, Style style) {
    for (uint16_t i = 0; i < len && y + i < rows_; ++i)
        set(x, y + i, Cell{ch, style});
}

void Buffer::resize(uint16_t cols, uint16_t rows) {
    cols_ = cols;
    rows_ = rows;
    cells_.assign(static_cast<size_t>(cols) * rows, EMPTY_CELL);
}

void Buffer::clear() {
    std::fill(cells_.begin(), cells_.end(), EMPTY_CELL);
}

// ── Screen ───────────────────────────────────────────────────────────────────

Screen::Screen(Terminal& term) : term_(term) {
    auto [c, r] = term_.size();
    front_ = Buffer(c, r);
    back_  = Buffer(c, r);
    out_buf_.reserve(4096);
}

void Screen::flush() {
    emit_diff();
    std::swap(front_, back_);
    back_.clear();
}

void Screen::force_redraw() {
    front_.clear();
}

void Screen::resize(uint16_t cols, uint16_t rows) {
    front_.resize(cols, rows);
    back_.resize(cols, rows);
}

void Screen::emit_diff() {
    auto cols = back_.cols();
    auto rows = back_.rows();

    out_buf_.clear();

    Style cur_style{};
    bool style_active = false;
    uint16_t cursor_x = 0xFFFF, cursor_y = 0xFFFF;

    auto move_to = [&](uint16_t x, uint16_t y) {
        if (cursor_x != x || cursor_y != y) {
            char buf[32];
            auto n = snprintf(buf, sizeof(buf), "\033[%d;%dH", y + 1, x + 1);
            out_buf_.append(buf, static_cast<size_t>(n));
            cursor_x = x;
            cursor_y = y;
        }
    };

    auto set_style = [&](const Style& s) {
        if (!style_active || !(cur_style == s)) {
            out_buf_ += tui::style_begin(s);
            if (s.is_default() && style_active)
                out_buf_ += "\033[0m";
            cur_style = s;
            style_active = !s.is_default();
        }
    };

    for (uint16_t y = 0; y < rows; ++y) {
        for (uint16_t x = 0; x < cols; ++x) {
            auto& bc = back_.get(x, y);
            auto& fc = front_.get(x, y);

            if (bc == fc) continue;

            move_to(x, y);
            set_style(bc.style);

            // Encode codepoint as UTF-8
            char32_t cp = bc.ch;
            if (cp < 0x80) {
                out_buf_ += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out_buf_ += static_cast<char>(0xC0 | (cp >> 6));
                out_buf_ += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out_buf_ += static_cast<char>(0xE0 | (cp >> 12));
                out_buf_ += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out_buf_ += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out_buf_ += static_cast<char>(0xF0 | (cp >> 18));
                out_buf_ += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out_buf_ += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out_buf_ += static_cast<char>(0x80 | (cp & 0x3F));
            }

            cursor_x = x + 1;
        }
    }

    if (style_active) out_buf_ += "\033[0m";

    // Single write syscall — no flicker
    if (!out_buf_.empty())
        ::write(STDOUT_FILENO, out_buf_.data(), out_buf_.size());
}

} // namespace clawed::ide
