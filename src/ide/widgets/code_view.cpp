#include <clawed/ide/widgets/code_view.hpp>
#include <clawed/tui/style.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace clawed::ide::widgets {

using tui::Style;
using tui::Color;
using tui::styles::gray;

static const Style kw_style   = Style{}.with_fg(Color::Blue);
static const Style str_style  = Style{}.with_fg(Color::Green);
static const Style cmt_style  = Style{}.with_fg(Color::BrightBlack);
static const Style num_style  = Style{}.with_fg(Color::Yellow);
static const Style prep_style = Style{}.with_fg(Color::Magenta);
static const Style line_style = Style{}.with_fg(Color::BrightBlack);
static const Style cursor_bg  = Style{}.with_bg(Color::BrightBlack).with_bold();

static const std::unordered_set<std::string> CPP_KEYWORDS = {
    "auto", "break", "case", "catch", "class", "concept", "const", "constexpr",
    "consteval", "constinit", "continue", "co_await", "co_return", "co_yield",
    "decltype", "default", "delete", "do", "else", "enum", "explicit", "export",
    "extern", "false", "for", "friend", "goto", "if", "inline", "mutable",
    "namespace", "new", "noexcept", "nullptr", "operator", "private", "protected",
    "public", "requires", "return", "sizeof", "static", "static_assert",
    "static_cast", "struct", "switch", "template", "this", "throw", "true",
    "try", "typedef", "typeid", "typename", "using", "virtual", "void",
    "volatile", "while", "override", "final",
    // Types
    "int", "long", "short", "char", "bool", "float", "double", "unsigned",
    "signed", "size_t", "string", "string_view", "vector", "unique_ptr",
    "shared_ptr", "optional", "variant", "expected", "pair", "tuple",
};

void CodeView::open_file(const std::string& path) {
    if (path == file_path_) return;
    file_path_ = path;
    lines_.clear();
    scroll_offset_ = 0;
    cursor_line_ = 0;

    std::ifstream f(path);
    if (!f.is_open()) {
        lines_.push_back("(cannot open file)");
        dirty_ = true;
        return;
    }

    std::string line;
    while (std::getline(f, line))
        lines_.push_back(std::move(line));

    if (lines_.empty()) lines_.push_back("");
    gutter_width_ = std::max(4, static_cast<int>(std::to_string(lines_.size()).size()) + 2);
    dirty_ = true;
}

void CodeView::scroll_to(int line) {
    cursor_line_ = std::clamp(line, 0, std::max(0, static_cast<int>(lines_.size()) - 1));
    dirty_ = true;
}

void CodeView::ensure_cursor_visible(int visible_lines) {
    if (cursor_line_ < scroll_offset_)
        scroll_offset_ = cursor_line_;
    if (cursor_line_ >= scroll_offset_ + visible_lines)
        scroll_offset_ = cursor_line_ - visible_lines + 1;
}

auto CodeView::highlight_line(const std::string& line) const
    -> std::vector<std::pair<std::string, Style>>
{
    std::vector<std::pair<std::string, Style>> spans;
    if (line.empty()) { spans.emplace_back("", Style{}); return spans; }

    // Preprocessor
    size_t first_nonspace = line.find_first_not_of(" \t");
    if (first_nonspace != std::string::npos && line[first_nonspace] == '#') {
        spans.emplace_back(line, prep_style);
        return spans;
    }

    size_t i = 0;
    while (i < line.size()) {
        // Comments
        if (i + 1 < line.size() && line[i] == '/' && line[i+1] == '/') {
            spans.emplace_back(line.substr(i), cmt_style);
            return spans;
        }

        // String literals
        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            size_t end = i + 1;
            while (end < line.size() && line[end] != quote) {
                if (line[end] == '\\') ++end;
                ++end;
            }
            if (end < line.size()) ++end;
            spans.emplace_back(line.substr(i, end - i), str_style);
            i = end;
            continue;
        }

        // Numbers
        if (std::isdigit(line[i]) || (line[i] == '.' && i + 1 < line.size() && std::isdigit(line[i+1]))) {
            size_t end = i;
            while (end < line.size() && (std::isalnum(line[end]) || line[end] == '.' || line[end] == 'x' || line[end] == '\''))
                ++end;
            spans.emplace_back(line.substr(i, end - i), num_style);
            i = end;
            continue;
        }

        // Identifiers / keywords
        if (std::isalpha(line[i]) || line[i] == '_') {
            size_t end = i;
            while (end < line.size() && (std::isalnum(line[end]) || line[end] == '_'))
                ++end;
            auto word = line.substr(i, end - i);
            if (CPP_KEYWORDS.count(word))
                spans.emplace_back(word, kw_style);
            else
                spans.emplace_back(word, Style{});
            i = end;
            continue;
        }

        // Other characters
        spans.emplace_back(std::string(1, line[i]), Style{});
        ++i;
    }

    return spans;
}

bool CodeView::on_event(const InputEvent& ev) {
    auto* ke = std::get_if<KeyEvent>(&ev);
    if (!ke) return false;

    int max_line = std::max(0, static_cast<int>(lines_.size()) - 1);

    switch (ke->key) {
        case KeyEvent::Up:
            if (cursor_line_ > 0) { --cursor_line_; dirty_ = true; }
            return true;
        case KeyEvent::Down:
            if (cursor_line_ < max_line) { ++cursor_line_; dirty_ = true; }
            return true;
        case KeyEvent::PageUp:
            cursor_line_ = std::max(0, cursor_line_ - 20);
            dirty_ = true;
            return true;
        case KeyEvent::PageDown:
            cursor_line_ = std::min(max_line, cursor_line_ + 20);
            dirty_ = true;
            return true;
        case KeyEvent::Home:
            cursor_line_ = 0; dirty_ = true;
            return true;
        case KeyEvent::End:
            cursor_line_ = max_line; dirty_ = true;
            return true;
        default:
            return false;
    }
}

void CodeView::render(Rect area, Buffer& buf) {
    dirty_ = false;
    if (area.height == 0 || area.width == 0) return;

    int visible = area.height;
    ensure_cursor_visible(visible);

    // Title bar
    if (!file_path_.empty()) {
        auto fname = std::filesystem::path(file_path_).filename().string();
        auto title = std::format(" {} ", fname);
        auto info = std::format("[{}/{}]", cursor_line_ + 1, lines_.size());

        buf.put_str(area.x, area.y, title, Style{}.with_fg(Color::Cyan).with_bold());
        if (info.size() + 1 < area.width)
            buf.put_str(area.x + area.width - static_cast<uint16_t>(info.size()) - 1,
                        area.y, info, gray);

        // Shift content area down
        area.y += 1;
        area.height -= 1;
        visible = area.height;
    }

    for (int row = 0; row < visible; ++row) {
        int line_idx = scroll_offset_ + row;
        auto y = static_cast<uint16_t>(area.y + row);

        if (line_idx >= static_cast<int>(lines_.size())) {
            buf.put_str(area.x, y, "~", gray);
            continue;
        }

        // Line number gutter
        bool is_cursor = (line_idx == cursor_line_);
        auto gutter_s = is_cursor ? Style{}.with_fg(Color::White).with_bold() : line_style;
        auto num = std::format("{:>{}}", line_idx + 1, gutter_width_ - 1);
        buf.put_str(area.x, y, num, gutter_s);
        buf.set(area.x + gutter_width_ - 1, y, Cell{' ', {}});

        // Code with syntax highlighting
        auto spans = highlight_line(lines_[line_idx]);
        uint16_t col = area.x + gutter_width_;
        for (auto& [text, style] : spans) {
            auto s = is_cursor ? Style{style}.with_bg(Color::BrightBlack) : style;
            col += buf.put_str(col, y, text, s);
            if (col >= area.x + area.width) break;
        }

        // Fill rest of cursor line with highlight
        if (is_cursor) {
            for (auto c = col; c < area.x + area.width; ++c)
                buf.set(c, y, Cell{' ', cursor_bg});
        }
    }
}

} // namespace clawed::ide::widgets
