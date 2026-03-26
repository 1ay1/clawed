#include <clawed/tui/input_view.hpp>
#include <iostream>
#include <string>

namespace clawed::tui {

auto InputView::read_line(std::string_view prompt) -> std::string {
    std::cout << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return {}; // EOF
    }
    return line;
}

} // namespace clawed::tui
