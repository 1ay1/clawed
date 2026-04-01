#pragma once

#include <clawed/ide/screen.hpp>
#include <clawed/ide/rect.hpp>
#include <clawed/ide/terminal.hpp>

#include <string>
#include <vector>

namespace clawed::ide::widgets {

class CodeView {
public:
    void open_file(const std::string& path);
    void scroll_to(int line);

    auto file_path() const -> const std::string& { return file_path_; }
    auto cursor_line() const -> int { return cursor_line_; }
    auto line_count() const -> int { return static_cast<int>(lines_.size()); }

    void render(Rect area, Buffer& buf);
    bool on_event(const InputEvent& ev);
    bool is_dirty() const { return dirty_; }

    bool dirty_ = true;

private:
    std::string file_path_;
    std::vector<std::string> lines_;
    int scroll_offset_ = 0;
    int cursor_line_ = 0;
    int gutter_width_ = 5;

    void ensure_cursor_visible(int visible_lines);

    // Simple syntax highlighting
    auto highlight_line(const std::string& line) const
        -> std::vector<std::pair<std::string, Style>>;
};

} // namespace clawed::ide::widgets
