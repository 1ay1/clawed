#include <clawed/ide/widgets/agent_chat.hpp>
#include <clawed/tui/style.hpp>
#include <format>

namespace clawed::ide::widgets {

using tui::Style;
using tui::Color;

void AgentChat::append_token(const std::string& text) {
    std::lock_guard lock(mu_);
    streaming_ += text;
    is_streaming_ = true;
    dirty_ = true;
}

void AgentChat::add_tool(const std::string& name, const std::string& id,
                         const std::string& summary) {
    std::lock_guard lock(mu_);
    flush_streaming();
    auto sid = id.size() > 8 ? id.substr(0, 8) : id;
    auto text = std::format("  * {} {}", name, summary.empty() ? sid : summary);
    lines_.push_back({Line::ToolOk, text});
    dirty_ = true;
}

void AgentChat::finish_tool(const std::string& id, bool is_error, int duration_ms) {
    std::lock_guard lock(mu_);
    // Find and update the last tool line matching this id
    auto sid = id.size() > 8 ? id.substr(0, 8) : id;
    for (auto it = lines_.rbegin(); it != lines_.rend(); ++it) {
        if ((it->kind == Line::ToolOk || it->kind == Line::ToolErr) &&
            it->content.find(sid) != std::string::npos) {
            it->kind = is_error ? Line::ToolErr : Line::ToolOk;
            it->content += std::format(" {}ms", duration_ms);
            break;
        }
    }
    dirty_ = true;
}

void AgentChat::finish_response() {
    std::lock_guard lock(mu_);
    flush_streaming();
    is_streaming_ = false;
    dirty_ = true;
}

void AgentChat::add_error(const std::string& msg) {
    std::lock_guard lock(mu_);
    flush_streaming();
    lines_.push_back({Line::Error, msg});
    dirty_ = true;
}

void AgentChat::flush_streaming() {
    if (!streaming_.empty()) {
        lines_.push_back({Line::Text, std::move(streaming_)});
        streaming_.clear();
    }
}

bool AgentChat::on_event(const InputEvent& ev) {
    auto* ke = std::get_if<KeyEvent>(&ev);
    if (!ke) return false;

    switch (ke->key) {
        case KeyEvent::Enter:
            if (!input_.empty()) {
                {
                    std::lock_guard lock(mu_);
                    lines_.push_back({Line::UserInput, input_});
                }
                auto msg = input_;
                input_.clear();
                input_cursor_ = 0;
                dirty_ = true;
                if (on_submit_) on_submit_(std::move(msg));
            }
            return true;

        case KeyEvent::Backspace:
            if (input_cursor_ > 0) {
                input_.erase(input_cursor_ - 1, 1);
                --input_cursor_;
                dirty_ = true;
            }
            return true;

        case KeyEvent::Char:
            if (ke->ch >= 32 && ke->ch < 127) {
                input_.insert(input_cursor_, 1, static_cast<char>(ke->ch));
                ++input_cursor_;
                dirty_ = true;
            }
            return true;

        case KeyEvent::Left:
            if (input_cursor_ > 0) { --input_cursor_; dirty_ = true; }
            return true;
        case KeyEvent::Right:
            if (input_cursor_ < static_cast<int>(input_.size())) { ++input_cursor_; dirty_ = true; }
            return true;

        case KeyEvent::PageUp:
            scroll_offset_ = std::max(0, scroll_offset_ - 10);
            dirty_ = true;
            return true;
        case KeyEvent::PageDown:
            scroll_offset_ += 10;
            dirty_ = true;
            return true;

        default:
            return false;
    }
}

void AgentChat::render(Rect area, Buffer& buf) {
    dirty_ = false;
    if (area.height < 3 || area.width < 5) return;

    std::lock_guard lock(mu_);

    // Input line at bottom (2 rows: border + input)
    uint16_t input_y = area.y + area.height - 1;
    auto prompt_style = Style{}.with_fg(Color::Magenta).with_bold();
    buf.put_str(area.x, input_y, "> ", prompt_style);
    buf.put_str(area.x + 2, input_y, input_, Style{});

    // Chat history area
    uint16_t chat_height = area.height - 1;

    // Build display lines (wrap long text)
    struct DisplayLine { std::string text; Style style; };
    std::vector<DisplayLine> display;

    auto wrap = [&](const std::string& text, Style style) {
        if (text.empty()) { display.push_back({"", style}); return; }
        size_t i = 0;
        while (i < text.size()) {
            auto chunk = text.substr(i, area.width);
            // Find newline
            auto nl = chunk.find('\n');
            if (nl != std::string::npos) chunk = chunk.substr(0, nl);
            display.push_back({chunk, style});
            i += chunk.size();
            if (i < text.size() && text[i] == '\n') ++i;
        }
    };

    for (auto& line : lines_) {
        switch (line.kind) {
            case Line::UserInput:
                wrap("> " + line.content, prompt_style);
                break;
            case Line::Text:
                wrap(line.content, Style{});
                break;
            case Line::ToolOk:
                wrap(line.content, Style{}.with_fg(Color::Green));
                break;
            case Line::ToolErr:
                wrap(line.content, Style{}.with_fg(Color::Red));
                break;
            case Line::Error:
                wrap("error: " + line.content, Style{}.with_fg(Color::Red).with_bold());
                break;
        }
    }

    // Streaming text
    if (is_streaming_ && !streaming_.empty())
        wrap(streaming_, Style{});

    // Auto-scroll to bottom
    int total = static_cast<int>(display.size());
    int start = std::max(0, total - chat_height);

    for (int row = 0; row < chat_height && (start + row) < total; ++row) {
        auto& dl = display[start + row];
        buf.put_str(area.x, area.y + row, dl.text, dl.style);
    }
}

} // namespace clawed::ide::widgets
