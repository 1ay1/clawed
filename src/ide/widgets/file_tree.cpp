#include <clawed/ide/widgets/file_tree.hpp>
#include <clawed/tui/style.hpp>

#include <algorithm>

namespace clawed::ide::widgets {

using tui::Style;
using tui::Color;

static const Style dir_style  = Style{}.with_fg(Color::Cyan).with_bold();
static const Style file_style = Style{};
static const Style sel_style  = Style{}.with_bg(Color::BrightBlack).with_bold();

void FileTree::set_root(const std::filesystem::path& root) {
    root_ = root;
    rebuild();
}

void FileTree::rebuild() {
    visible_.clear();
    selected_ = 0;
    scroll_offset_ = 0;
    add_children(root_, 0);
    dirty_ = true;
}

void FileTree::add_children(const std::filesystem::path& dir, int depth) {
    namespace fs = std::filesystem;

    std::vector<fs::directory_entry> entries;
    try {
        for (auto& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied))
            entries.push_back(e);
    } catch (...) { return; }

    // Sort: directories first, then alphabetical
    std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) {
        if (a.is_directory() != b.is_directory()) return a.is_directory();
        return a.path().filename() < b.path().filename();
    });

    for (auto& e : entries) {
        auto name = e.path().filename().string();
        // Skip hidden files/dirs
        if (name.empty() || name[0] == '.') continue;
        // Skip build directory
        if (name == "build" && depth == 0) continue;

        Node node;
        node.name      = name;
        node.full_path = e.path().string();
        node.is_dir    = e.is_directory();
        node.depth     = depth;
        node.expanded  = false;
        visible_.push_back(std::move(node));
    }
}

void FileTree::toggle_expand() {
    if (selected_ < 0 || selected_ >= static_cast<int>(visible_.size())) return;
    auto& node = visible_[selected_];

    if (!node.is_dir) {
        if (on_select_) on_select_(node.full_path);
        return;
    }

    if (node.expanded) {
        // Collapse: remove children
        node.expanded = false;
        int rm_start = selected_ + 1;
        int rm_end = rm_start;
        while (rm_end < static_cast<int>(visible_.size()) && visible_[rm_end].depth > node.depth)
            ++rm_end;
        visible_.erase(visible_.begin() + rm_start, visible_.begin() + rm_end);
    } else {
        // Expand: insert children after this node
        node.expanded = true;
        std::vector<Node> children;
        // Temporarily collect into children vector
        auto old_size = visible_.size();
        add_children(node.full_path, node.depth + 1);
        // The add_children appended to visible_ — move them to the right position
        auto new_count = visible_.size() - old_size;
        if (new_count > 0) {
            std::vector<Node> inserted(
                std::make_move_iterator(visible_.begin() + old_size),
                std::make_move_iterator(visible_.end()));
            visible_.erase(visible_.begin() + old_size, visible_.end());
            visible_.insert(visible_.begin() + selected_ + 1,
                std::make_move_iterator(inserted.begin()),
                std::make_move_iterator(inserted.end()));
        }
    }
    dirty_ = true;
}

bool FileTree::on_event(const InputEvent& ev) {
    auto* ke = std::get_if<KeyEvent>(&ev);
    if (!ke) return false;

    int max_idx = std::max(0, static_cast<int>(visible_.size()) - 1);

    switch (ke->key) {
        case KeyEvent::Up:
            if (selected_ > 0) { --selected_; dirty_ = true; }
            return true;
        case KeyEvent::Down:
            if (selected_ < max_idx) { ++selected_; dirty_ = true; }
            return true;
        case KeyEvent::Enter:
            toggle_expand();
            return true;
        case KeyEvent::Right:
            if (selected_ < static_cast<int>(visible_.size()) &&
                visible_[selected_].is_dir && !visible_[selected_].expanded)
                toggle_expand();
            return true;
        case KeyEvent::Left:
            if (selected_ < static_cast<int>(visible_.size()) &&
                visible_[selected_].is_dir && visible_[selected_].expanded)
                toggle_expand();
            return true;
        default:
            return false;
    }
}

void FileTree::render(Rect area, Buffer& buf) {
    dirty_ = false;
    if (area.height == 0 || area.width == 0) return;

    // Title
    buf.put_str(area.x, area.y, " Files", Style{}.with_fg(Color::Cyan).with_bold());
    area.y += 1;
    area.height -= 1;

    // Ensure selected is visible
    int visible_count = area.height;
    if (selected_ < scroll_offset_)
        scroll_offset_ = selected_;
    if (selected_ >= scroll_offset_ + visible_count)
        scroll_offset_ = selected_ - visible_count + 1;

    for (int row = 0; row < visible_count; ++row) {
        int idx = scroll_offset_ + row;
        auto y = static_cast<uint16_t>(area.y + row);

        if (idx >= static_cast<int>(visible_.size())) break;

        auto& node = visible_[idx];
        bool is_sel = (idx == selected_);

        // Indentation
        int indent = node.depth * 2;
        std::string prefix(indent, ' ');

        // Directory marker
        if (node.is_dir) {
            prefix += node.expanded ? "v " : "> ";
        } else {
            prefix += "  ";
        }

        auto name = prefix + node.name;
        if (name.size() > area.width) name = name.substr(0, area.width);

        Style style;
        if (is_sel) {
            style = sel_style;
            buf.fill(Rect{area.x, y, area.width, 1}, ' ', sel_style);
        }
        if (node.is_dir)
            style = is_sel ? Style{}.with_fg(Color::Cyan).with_bg(Color::BrightBlack).with_bold()
                           : dir_style;
        else
            style = is_sel ? sel_style : file_style;

        buf.put_str(area.x, y, name, style);
    }
}

} // namespace clawed::ide::widgets
