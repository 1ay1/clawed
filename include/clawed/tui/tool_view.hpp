#pragma once

#include <clawed/tui/stream_view.hpp>
#include <string>

namespace clawed::tui {

// Terminal formatting for tool call display.

class ToolView {
public:
    static auto format_tool_start(std::string_view name, std::string_view id)
        -> std::string;
    static auto format_tool_end(const ToolCallCard& card) -> std::string;
};

} // namespace clawed::tui
