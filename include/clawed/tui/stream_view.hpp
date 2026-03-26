#pragma once

#include <clawed/core/state_machine.hpp>
#include <mutex>
#include <string>
#include <vector>

namespace clawed::tui {

// Holds accumulated output for rendering.
// Thread-safe: agent thread writes, TUI thread reads.

struct ToolCallCard {
    std::string name;
    std::string id;
    std::string result;
    bool        is_error  = false;
    bool        completed = false;
};

class StreamView {
public:
    void append_token(std::string_view text);
    void begin_tool_call(std::string_view name, std::string_view id);
    void end_tool_call(std::string_view id, std::string_view result, bool is_error);
    void set_status(std::string_view msg);
    void set_error(std::string_view msg);
    void mark_done();
    void clear_current();

    // Snapshot for rendering (copies under lock).
    struct Snapshot {
        std::string             text;
        std::vector<ToolCallCard> tool_cards;
        std::string             status;
        std::string             error;
        bool                    done = false;
    };

    [[nodiscard]] auto snapshot() -> Snapshot;

private:
    mutable std::mutex mu_;
    std::string             text_;
    std::vector<ToolCallCard> tool_cards_;
    std::string             status_;
    std::string             error_;
    bool                    done_ = false;
};

} // namespace clawed::tui
