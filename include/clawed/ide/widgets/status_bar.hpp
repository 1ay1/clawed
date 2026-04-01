#pragma once

#include <clawed/ide/screen.hpp>
#include <clawed/ide/rect.hpp>
#include <clawed/ide/terminal.hpp>
#include <string>

namespace clawed::ide::widgets {

class StatusBar {
public:
    void set_file(const std::string& path)          { file_ = path; dirty_ = true; }
    void set_position(int line, int col)             { line_ = line; col_ = col; dirty_ = true; }
    void set_agent_status(const std::string& status) { status_ = status; dirty_ = true; }

    void render(Rect area, Buffer& buf);
    bool on_event(const InputEvent&) { return false; }
    bool is_dirty() const { return dirty_; }

    bool dirty_ = true;

private:
    std::string file_;
    int line_ = 0, col_ = 0;
    std::string status_ = "idle";
};

} // namespace clawed::ide::widgets
