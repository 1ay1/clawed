#pragma once

#include <clawed/ide/screen.hpp>
#include <clawed/ide/rect.hpp>
#include <clawed/ide/terminal.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace clawed::ide::widgets {

class FileTree {
public:
    using OnSelect = std::function<void(const std::string&)>;

    void set_root(const std::filesystem::path& root);
    void set_on_select(OnSelect cb) { on_select_ = std::move(cb); }

    void render(Rect area, Buffer& buf);
    bool on_event(const InputEvent& ev);
    bool is_dirty() const { return dirty_; }

    bool dirty_ = true;

private:
    struct Node {
        std::string name;
        std::string full_path;
        bool is_dir   = false;
        bool expanded = false;
        int  depth    = 0;
    };

    std::filesystem::path root_;
    std::vector<Node> visible_; // flattened visible nodes
    int selected_ = 0;
    int scroll_offset_ = 0;
    OnSelect on_select_;

    void rebuild();
    void add_children(const std::filesystem::path& dir, int depth);
    void toggle_expand();
};

} // namespace clawed::ide::widgets
