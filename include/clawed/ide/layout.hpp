#pragma once

#include <clawed/ide/rect.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace clawed::ide {

struct Constraint {
    enum Kind : uint8_t { Fixed, Flex, Percent };
    Kind     kind;
    uint16_t value;

    static constexpr auto fixed(uint16_t n)   { return Constraint{Fixed, n}; }
    static constexpr auto flex(uint16_t r = 1) { return Constraint{Flex, r}; }
    static constexpr auto percent(uint16_t p) { return Constraint{Percent, p}; }
};

enum class Direction : uint8_t { Horizontal, Vertical };

/// Split a rect into sub-rects according to constraints.
auto layout_split(Rect area, Direction dir,
                  std::span<const Constraint> constraints)
    -> std::vector<Rect>;

} // namespace clawed::ide
