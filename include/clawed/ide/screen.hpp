#pragma once

#include <clawed/ide/rect.hpp>
#include <clawed/ide/terminal.hpp>
#include <clawed/tui/style.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace clawed::ide {

using tui::Style;
using tui::Color;

// ── Cell ─────────────────────────────────────────────────────────────────────

struct Cell {
    char32_t ch    = ' ';
    Style    style = {};

    bool operator==(const Cell&) const = default;
};

// ── Buffer ───────────────────────────────────────────────────────────────────

class Buffer {
public:
    Buffer() = default;
    Buffer(uint16_t cols, uint16_t rows);

    auto get(uint16_t x, uint16_t y) const -> const Cell&;
    void set(uint16_t x, uint16_t y, Cell cell);

    /// Write a string at (x,y) clipped to buffer bounds. Returns columns consumed.
    auto put_str(uint16_t x, uint16_t y, std::string_view text, Style style = {}) -> uint16_t;

    /// Fill a rect with a cell.
    void fill(Rect area, Cell cell);

    /// Fill a rect with a character + style.
    void fill(Rect area, char32_t ch, Style style = {});

    /// Draw a horizontal line.
    void hline(uint16_t x, uint16_t y, uint16_t len, char32_t ch, Style style = {});

    /// Draw a vertical line.
    void vline(uint16_t x, uint16_t y, uint16_t len, char32_t ch, Style style = {});

    void resize(uint16_t cols, uint16_t rows);
    void clear();

    [[nodiscard]] auto cols() const -> uint16_t { return cols_; }
    [[nodiscard]] auto rows() const -> uint16_t { return rows_; }

private:
    uint16_t cols_ = 0, rows_ = 0;
    std::vector<Cell> cells_;

    [[nodiscard]] auto idx(uint16_t x, uint16_t y) const -> size_t {
        return static_cast<size_t>(y) * cols_ + x;
    }
    [[nodiscard]] bool in_bounds(uint16_t x, uint16_t y) const {
        return x < cols_ && y < rows_;
    }
};

// ── Screen ───────────────────────────────────────────────────────────────────

class Screen {
public:
    explicit Screen(Terminal& term);

    /// Get the back buffer for drawing.
    auto back() -> Buffer& { return back_; }

    /// Diff back vs front, emit only changed cells, swap buffers.
    void flush();

    /// Mark everything dirty (forces full redraw on next flush).
    void force_redraw();

    /// Resize both buffers.
    void resize(uint16_t cols, uint16_t rows);

private:
    Terminal& term_;
    Buffer front_, back_;
    std::string out_buf_; // reusable output accumulator

    void emit_diff();
};

} // namespace clawed::ide
