#include <clawed/ide/layout.hpp>
#include <algorithm>
#include <numeric>

namespace clawed::ide {

auto layout_split(Rect area, Direction dir,
                  std::span<const Constraint> constraints)
    -> std::vector<Rect>
{
    if (constraints.empty()) return {};

    uint16_t total = (dir == Direction::Horizontal) ? area.width : area.height;

    // Pass 1: allocate fixed + percent, compute remaining for flex
    uint16_t used = 0;
    uint16_t flex_total = 0;

    for (auto& c : constraints) {
        if (c.kind == Constraint::Fixed)
            used += c.value;
        else if (c.kind == Constraint::Percent)
            used += static_cast<uint16_t>(total * c.value / 100);
        else
            flex_total += c.value;
    }

    uint16_t remaining = (used < total) ? (total - used) : 0;

    // Pass 2: compute sizes
    std::vector<uint16_t> sizes;
    sizes.reserve(constraints.size());

    for (auto& c : constraints) {
        switch (c.kind) {
            case Constraint::Fixed:
                sizes.push_back(c.value);
                break;
            case Constraint::Percent:
                sizes.push_back(static_cast<uint16_t>(total * c.value / 100));
                break;
            case Constraint::Flex:
                sizes.push_back(flex_total > 0
                    ? static_cast<uint16_t>(remaining * c.value / flex_total)
                    : 0);
                break;
        }
    }

    // Build rects
    std::vector<Rect> result;
    result.reserve(constraints.size());
    uint16_t offset = 0;

    for (auto sz : sizes) {
        if (dir == Direction::Horizontal) {
            result.push_back({
                static_cast<uint16_t>(area.x + offset), area.y,
                std::min(sz, static_cast<uint16_t>(total - offset)), area.height
            });
        } else {
            result.push_back({
                area.x, static_cast<uint16_t>(area.y + offset),
                area.width, std::min(sz, static_cast<uint16_t>(total - offset))
            });
        }
        offset += sz;
    }

    return result;
}

} // namespace clawed::ide
