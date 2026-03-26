#include <clawed/tui/tool_view.hpp>
#include <format>

namespace clawed::tui {

auto ToolView::format_tool_start(std::string_view name, std::string_view id)
    -> std::string
{
    return std::format("\033[90m--- tool: {} [{}] ---\033[0m\n",
                       name, id.substr(0, 8));
}

auto ToolView::format_tool_end(const ToolCallCard& card) -> std::string {
    std::string result;
    if (card.is_error) {
        result += std::format("\033[31m[error] {}\033[0m\n", card.result);
    } else {
        // Truncate long results for display.
        auto display = card.result;
        if (display.size() > 500) {
            display = display.substr(0, 500) + "... [truncated]";
        }
        result += std::format("\033[90m{}\033[0m\n", display);
    }
    result += std::format("\033[90m--- end {} ---\033[0m\n", card.name);
    return result;
}

} // namespace clawed::tui
