#pragma once

#include <string>

namespace clawed::tui {

// Simple line input component.
// In v0.1 we use basic terminal readline. FTXUI input in v0.2.

class InputView {
public:
    // Read a line from the user. Returns empty on EOF/ctrl-D.
    [[nodiscard]] auto read_line(std::string_view prompt) -> std::string;
};

} // namespace clawed::tui
