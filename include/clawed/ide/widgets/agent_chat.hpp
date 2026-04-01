#pragma once

#include <clawed/ide/screen.hpp>
#include <clawed/ide/rect.hpp>
#include <clawed/ide/terminal.hpp>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace clawed::ide::widgets {

class AgentChat {
public:
    using OnSubmit = std::function<void(std::string)>;
    void set_on_submit(OnSubmit cb) { on_submit_ = std::move(cb); }

    // Thread-safe updates from agent
    void append_token(const std::string& text);
    void add_tool(const std::string& name, const std::string& id,
                  const std::string& summary = {});
    void finish_tool(const std::string& id, bool is_error, int duration_ms);
    void finish_response();
    void add_error(const std::string& msg);

    void render(Rect area, Buffer& buf);
    bool on_event(const InputEvent& ev);
    bool is_dirty() const { return dirty_; }

    bool dirty_ = true;

private:
    struct Line {
        enum Kind { UserInput, Text, ToolOk, ToolErr, Error };
        Kind kind;
        std::string content;
    };

    mutable std::mutex mu_;
    std::vector<Line> lines_;         // conversation history
    std::string streaming_;           // current streaming text
    bool is_streaming_ = false;

    std::string input_;               // current input buffer
    int input_cursor_ = 0;
    int scroll_offset_ = 0;
    OnSubmit on_submit_;

    void flush_streaming();
    auto total_display_lines(uint16_t width) const -> int;
};

} // namespace clawed::ide::widgets
