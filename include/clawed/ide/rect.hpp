#pragma once

#include <algorithm>
#include <cstdint>

namespace clawed::ide {

struct Rect {
    uint16_t x = 0, y = 0, width = 0, height = 0;

    [[nodiscard]] constexpr auto hsplit(uint16_t left_w) const -> std::pair<Rect, Rect> {
        auto lw = std::min(left_w, width);
        return {
            {x, y, lw, height},
            {static_cast<uint16_t>(x + lw), y, static_cast<uint16_t>(width - lw), height}
        };
    }

    [[nodiscard]] constexpr auto vsplit(uint16_t top_h) const -> std::pair<Rect, Rect> {
        auto th = std::min(top_h, height);
        return {
            {x, y, width, th},
            {x, static_cast<uint16_t>(y + th), width, static_cast<uint16_t>(height - th)}
        };
    }

    [[nodiscard]] constexpr auto inner() const -> Rect {
        if (width < 2 || height < 2) return *this;
        return {static_cast<uint16_t>(x + 1), static_cast<uint16_t>(y + 1),
                static_cast<uint16_t>(width - 2), static_cast<uint16_t>(height - 2)};
    }

    [[nodiscard]] constexpr bool contains(uint16_t px, uint16_t py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }

    [[nodiscard]] constexpr bool empty() const { return width == 0 || height == 0; }
};

} // namespace clawed::ide
