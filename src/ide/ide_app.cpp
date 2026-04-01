#include <clawed/ide/ide_app.hpp>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <unordered_set>

namespace clawed::ide {

namespace fs = std::filesystem;
using namespace ftxui;

// ── Helpers ──────────────────────────────────────────────────────────────────

static auto read_file_lines(const std::string& path) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::ifstream f(path);
    if (!f.is_open()) return lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(std::move(line));
    if (lines.empty()) lines.push_back("");
    return lines;
}

static auto syntax_color(const std::string& token) -> Color {
    static const std::unordered_set<std::string> keywords = {
        "auto","break","case","catch","class","concept","const","constexpr",
        "continue","default","delete","do","else","enum","explicit","extern",
        "false","for","friend","if","inline","namespace","new","noexcept",
        "nullptr","operator","private","protected","public","requires","return",
        "sizeof","static","static_assert","static_cast","struct","switch",
        "template","this","throw","true","try","typedef","typename","using",
        "virtual","void","volatile","while","override","final",
        "int","long","short","char","bool","float","double","unsigned",
        "string","string_view","vector","size_t","uint16_t","uint32_t",
    };
    if (keywords.count(token)) return Color::Blue;
    return Color::Default;
}

// ── Simple syntax highlighted line ───────────────────────────────────────────

static auto highlight_line(const std::string& line) -> Element {
    Elements spans;
    size_t i = 0;

    while (i < line.size()) {
        // Comment
        if (i + 1 < line.size() && line[i] == '/' && line[i+1] == '/') {
            spans.push_back(text(line.substr(i)) | color(Color::GrayDark));
            return hbox(std::move(spans));
        }
        // String
        if (line[i] == '"' || line[i] == '\'') {
            char q = line[i];
            size_t end = i + 1;
            while (end < line.size() && line[end] != q) {
                if (line[end] == '\\') ++end;
                ++end;
            }
            if (end < line.size()) ++end;
            spans.push_back(text(line.substr(i, end - i)) | color(Color::Green));
            i = end;
            continue;
        }
        // Preprocessor
        if (line[i] == '#' && (i == 0 || line.substr(0, i).find_first_not_of(" \t") == std::string::npos)) {
            spans.push_back(text(line.substr(i)) | color(Color::Magenta));
            return hbox(std::move(spans));
        }
        // Number
        if (std::isdigit(line[i])) {
            size_t end = i;
            while (end < line.size() && (std::isalnum(line[end]) || line[end] == '.' || line[end] == '\''))
                ++end;
            spans.push_back(text(line.substr(i, end - i)) | color(Color::Yellow));
            i = end;
            continue;
        }
        // Identifier/keyword
        if (std::isalpha(line[i]) || line[i] == '_') {
            size_t end = i;
            while (end < line.size() && (std::isalnum(line[end]) || line[end] == '_'))
                ++end;
            auto word = line.substr(i, end - i);
            auto c = syntax_color(word);
            spans.push_back(text(word) | color(c));
            i = end;
            continue;
        }
        // Other
        spans.push_back(text(std::string(1, line[i])));
        ++i;
    }

    if (spans.empty()) return text("");
    return hbox(std::move(spans));
}

// ── File Tree entries ────────────────────────────────────────────────────────

struct TreeEntry {
    std::string name;
    std::string path;
    bool is_dir;
    int depth;
    bool expanded = false;
};

static void populate_tree(std::vector<TreeEntry>& entries, const fs::path& dir, int depth) {
    std::vector<fs::directory_entry> items;
    try {
        for (auto& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied))
            items.push_back(e);
    } catch (...) { return; }

    std::sort(items.begin(), items.end(), [](auto& a, auto& b) {
        if (a.is_directory() != b.is_directory()) return a.is_directory();
        return a.path().filename() < b.path().filename();
    });

    for (auto& e : items) {
        auto name = e.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (name == "build" && depth == 0) continue;
        entries.push_back({name, e.path().string(), e.is_directory(), depth});
    }
}

// ── IdeApp ───────────────────────────────────────────────────────────────────

IdeApp::IdeApp(ApiClient& client, ToolRegistry& registry,
               AgentLoop::Config agent_config)
    : client_(client)
    , registry_(registry)
    , agent_(client_, registry_, std::move(agent_config))
{}

void IdeApp::run() {
    auto screen = ScreenInteractive::Fullscreen();

    // ── File tree state ──────────────────────────────────────────────────
    std::vector<TreeEntry> tree_entries;
    populate_tree(tree_entries, fs::current_path(), 0);
    int tree_selected = 0;

    // ── Code view state ──────────────────────────────────────────────────
    int code_scroll = 0;
    int code_cursor = 0;

    // ── Chat state ───────────────────────────────────────────────────────
    std::string chat_input;
    [[maybe_unused]] int chat_scroll = 0;

    // ── Focus ────────────────────────────────────────────────────────────
    enum Focus { FocusTree, FocusCode, FocusChat };
    Focus focus = FocusChat;

    // ── Open a file ──────────────────────────────────────────────────────
    auto open_file = [&](const std::string& path) {
        std::lock_guard lock(state_.mu);
        state_.current_file = path;
        state_.file_lines = read_file_lines(path);
        code_scroll = 0;
        code_cursor = 0;
    };

    // ── Build UI sink for agent ──────────────────────────────────────────
    auto ui_sink = [&](UiEvent evt) {
        std::visit(Overloaded{
            [&](UiTokens& t) {
                std::lock_guard lock(state_.mu);
                state_.streaming_text += t.text;
                state_.is_streaming = true;
                state_.dirty = true;
            },
            [&](UiToolQueued& t) {
                std::lock_guard lock(state_.mu);
                state_.tools.push_back({t.name, t.id, {}, SharedState::ToolInfo::Queued});
                state_.dirty = true;
            },
            [&](UiToolRunning& t) {
                std::lock_guard lock(state_.mu);
                for (auto& tool : state_.tools) {
                    if (tool.id == t.id) {
                        tool.state = SharedState::ToolInfo::Running;
                        tool.summary = t.summary;
                        break;
                    }
                }
                state_.agent_status = "running: " + t.name;
                state_.dirty = true;

                // Auto-open files
                if (t.name == "read_file" || t.name == "edit") {
                    // open_file needs to happen on UI thread, store path
                    state_.current_file = t.summary;
                    state_.file_lines = read_file_lines(t.summary);
                }
            },
            [&](UiToolEnd& t) {
                std::lock_guard lock(state_.mu);
                for (auto& tool : state_.tools) {
                    if (tool.id == t.id) {
                        tool.state = t.is_error ? SharedState::ToolInfo::Failed
                                                : SharedState::ToolInfo::Done;
                        tool.duration_ms = t.duration_ms;
                        break;
                    }
                }
                state_.dirty = true;
            },
            [&](UiStatus&) {
                std::lock_guard lock(state_.mu);
                state_.agent_status = "thinking";
                state_.tools.clear();
                state_.dirty = true;
            },
            [&](UiError& e) {
                std::lock_guard lock(state_.mu);
                if (!state_.streaming_text.empty()) {
                    state_.chat_lines.push_back(std::move(state_.streaming_text));
                    state_.streaming_text.clear();
                }
                state_.chat_lines.push_back("error: " + e.error.message);
                state_.agent_status = "error";
                state_.dirty = true;
            },
            [&](UiDone&) {
                std::lock_guard lock(state_.mu);
                if (!state_.streaming_text.empty()) {
                    state_.chat_lines.push_back(std::move(state_.streaming_text));
                    state_.streaming_text.clear();
                }
                state_.is_streaming = false;
                state_.agent_status = "idle";
                state_.dirty = true;
            },
        }, evt);
        screen.PostEvent(Event::Custom);
    };

    // ── Submit ────────────────────────────────────────────────────────────
    auto do_submit = [&]() {
        if (chat_input.empty()) return;
        auto msg = chat_input;
        chat_input.clear();

        {
            std::lock_guard lock(state_.mu);
            state_.chat_lines.push_back("> " + msg);
            state_.agent_status = "thinking";
            state_.dirty = true;
        }

        if (agent_thread_.joinable()) agent_thread_.join();

        auto sink = ui_sink;
        agent_thread_ = std::jthread([this, msg = std::move(msg), sink = std::move(sink)]() {
            auto result = agent_.run_turn(msg, sink);
            if (!result) {
                sink(UiError{result.error()});
            }
        });
    };

    // ── FTXUI Component ──────────────────────────────────────────────────
    auto component = Renderer([&] {
        std::lock_guard lock(state_.mu);

        // ── File tree ────────────────────────────────────────────────
        Elements tree_items;
        for (int i = 0; i < static_cast<int>(tree_entries.size()); ++i) {
            auto& e = tree_entries[i];
            std::string indent(e.depth * 2, ' ');
            std::string prefix = e.is_dir ? (e.expanded ? "v " : "> ") : "  ";
            auto line_text = indent + prefix + e.name;

            auto el = text(line_text);
            if (e.is_dir) el = el | color(Color::Cyan);
            if (i == tree_selected && focus == FocusTree)
                el = el | inverted;
            tree_items.push_back(el);
        }

        auto file_tree = vbox(std::move(tree_items)) | vscroll_indicator | frame |
            borderStyled(focus == FocusTree ? ROUNDED : LIGHT) |
            size(WIDTH, EQUAL, 24);

        // ── Code view ────────────────────────────────────────────────
        Elements code_lines;
        auto& fl = state_.file_lines;
        int visible = 30; // approximate
        int start = std::max(0, code_cursor - visible / 2);

        for (int i = start; i < static_cast<int>(fl.size()) && i < start + visible; ++i) {
            auto num = text(std::format("{:>4} ", i + 1)) | color(Color::GrayDark);
            auto code = highlight_line(fl[i]);
            auto line_el = hbox({num, code});
            if (i == code_cursor && focus == FocusCode)
                line_el = line_el | inverted;
            code_lines.push_back(line_el);
        }

        std::string code_title = state_.current_file.empty()
            ? " Code "
            : std::format(" {} [{}/{}] ",
                fs::path(state_.current_file).filename().string(),
                code_cursor + 1, fl.size());

        auto code_view = window(text(code_title) | color(Color::Cyan),
            vbox(std::move(code_lines)) | vscroll_indicator | frame) |
            borderStyled(focus == FocusCode ? ROUNDED : LIGHT) | flex;

        // ── Chat panel ───────────────────────────────────────────────
        Elements chat_items;
        for (auto& line : state_.chat_lines) {
            if (line.starts_with("> "))
                chat_items.push_back(text(line) | color(Color::Magenta) | bold);
            else if (line.starts_with("error:"))
                chat_items.push_back(text(line) | color(Color::Red));
            else
                chat_items.push_back(paragraph(line));
        }
        // Streaming text
        if (state_.is_streaming && !state_.streaming_text.empty())
            chat_items.push_back(paragraph(state_.streaming_text) | color(Color::White));

        // Tool indicators
        for (auto& t : state_.tools) {
            auto sid = t.id.size() > 8 ? t.id.substr(0, 8) : t.id;
            std::string icon;
            Color c;
            switch (t.state) {
                case SharedState::ToolInfo::Queued:
                    icon = "○"; c = Color::GrayDark; break;
                case SharedState::ToolInfo::Running:
                    icon = "●"; c = Color::Yellow; break;
                case SharedState::ToolInfo::Done:
                    icon = "✓"; c = Color::Green; break;
                case SharedState::ToolInfo::Failed:
                    icon = "✗"; c = Color::Red; break;
            }
            auto label = t.summary.empty() ? t.name : t.name + " " + t.summary;
            if (label.size() > 50) label = label.substr(0, 50) + "...";
            if (t.duration_ms > 0)
                label += std::format(" {}ms", t.duration_ms);
            chat_items.push_back(hbox({
                text(" " + icon + " ") | color(c),
                text(label) | color(Color::GrayLight),
            }));
        }

        // Input line
        auto input_line = hbox({
            text("> ") | color(Color::Magenta) | bold,
            text(chat_input) | (focus == FocusChat ? inverted : nothing),
        });

        auto chat_panel = vbox({
            vbox(std::move(chat_items)) | vscroll_indicator | frame | flex,
            separator(),
            input_line,
        }) | borderStyled(focus == FocusChat ? ROUNDED : LIGHT);

        // ── Status bar ───────────────────────────────────────────────
        auto status = hbox({
            text(" " + state_.current_file + " ") | color(Color::White),
            filler(),
            text(" " + state_.agent_status + " ") | color(
                state_.agent_status == "idle" ? Color::Green :
                state_.agent_status == "error" ? Color::Red : Color::Yellow),
            text(" clawed ") | color(Color::Cyan) | bold,
        }) | inverted;

        // ── Layout (Zed-style) ───────────────────────────────────────
        return vbox({
            hbox({
                file_tree,
                code_view,
            }) | flex_grow,
            chat_panel | size(HEIGHT, EQUAL, 12),
            status,
        });
    });

    // ── Event handling ────────────────────────────────────────────────────
    component = CatchEvent(component, [&](Event event) -> bool {
        // Ctrl+Q quit
        if (event == Event::Character('q') && event.is_character()) {
            // Check if ctrl (event character with low codepoint)
        }
        if (event.input() == std::string(1, 17)) { // Ctrl+Q
            if (agent_thread_.joinable()) {
                agent_.request_stop();
                agent_thread_.join();
            }
            screen.Exit();
            return true;
        }

        // Tab to switch focus
        if (event == Event::Tab) {
            focus = static_cast<Focus>((focus + 1) % 3);
            return true;
        }
        if (event == Event::TabReverse) {
            focus = static_cast<Focus>((focus + 2) % 3);
            return true;
        }

        // Focus-specific handling
        if (focus == FocusChat) {
            if (event == Event::Return) {
                do_submit();
                return true;
            }
            if (event.is_character()) {
                chat_input += event.character();
                return true;
            }
            if (event == Event::Backspace && !chat_input.empty()) {
                chat_input.pop_back();
                return true;
            }
            return false;
        }

        if (focus == FocusTree) {
            if (event == Event::ArrowUp && tree_selected > 0) {
                --tree_selected; return true;
            }
            if (event == Event::ArrowDown &&
                tree_selected < static_cast<int>(tree_entries.size()) - 1) {
                ++tree_selected; return true;
            }
            if (event == Event::Return && tree_selected < static_cast<int>(tree_entries.size())) {
                auto& entry = tree_entries[tree_selected];
                if (entry.is_dir) {
                    if (entry.expanded) {
                        entry.expanded = false;
                        // Remove children
                        int rm = tree_selected + 1;
                        while (rm < static_cast<int>(tree_entries.size()) &&
                               tree_entries[rm].depth > entry.depth)
                            ++rm;
                        tree_entries.erase(tree_entries.begin() + tree_selected + 1,
                                          tree_entries.begin() + rm);
                    } else {
                        entry.expanded = true;
                        std::vector<TreeEntry> children;
                        populate_tree(children, entry.path, entry.depth + 1);
                        tree_entries.insert(tree_entries.begin() + tree_selected + 1,
                            children.begin(), children.end());
                    }
                } else {
                    open_file(entry.path);
                }
                return true;
            }
            return false;
        }

        if (focus == FocusCode) {
            std::lock_guard lock(state_.mu);
            int max_line = std::max(0, static_cast<int>(state_.file_lines.size()) - 1);
            if (event == Event::ArrowUp && code_cursor > 0) {
                --code_cursor; return true;
            }
            if (event == Event::ArrowDown && code_cursor < max_line) {
                ++code_cursor; return true;
            }
            if (event == Event::PageUp) {
                code_cursor = std::max(0, code_cursor - 20); return true;
            }
            if (event == Event::PageDown) {
                code_cursor = std::min(max_line, code_cursor + 20); return true;
            }
            return false;
        }

        return false;
    });

    screen.Loop(component);

    // Cleanup
    if (agent_thread_.joinable()) {
        agent_.request_stop();
        agent_thread_.join();
    }
}

void IdeApp::submit_message(const std::string& msg) {
    // Not used — inline in run()
}

auto IdeApp::build_ui_sink() -> UiSink {
    return [](UiEvent) {}; // Not used — inline in run()
}

} // namespace clawed::ide
