#include <clawed/ide/ide_app.hpp>
#include <clawed/ide/layout.hpp>
#include <clawed/tui/style.hpp>

#include <filesystem>

namespace clawed::ide {

using tui::Style;
using tui::Color;

IdeApp::IdeApp(ApiClient& client, ToolRegistry& registry,
               AgentLoop::Config agent_config)
    : screen_(terminal_)
    , agent_(client, registry, std::move(agent_config))
{
    auto cwd = std::filesystem::current_path();
    file_tree_.set_root(cwd);

    file_tree_.set_on_select([this](const std::string& path) {
        code_view_.open_file(path);
        status_bar_.set_file(path);
    });

    agent_chat_.set_on_submit([this](std::string msg) {
        submit_to_agent(std::move(msg));
    });

    status_bar_.set_agent_status("idle");
}

void IdeApp::run() {
    terminal_.enable_raw_mode();
    terminal_.enter_alt_screen();
    terminal_.hide_cursor();

    auto [cols, rows] = terminal_.size();
    screen_.resize(cols, rows);
    screen_.force_redraw();

    while (running_) {
        // 1. Poll input (~60fps)
        if (auto ev = terminal_.read_event(16)) {
            if (auto* re = std::get_if<ResizeEvent>(&*ev)) {
                screen_.resize(re->cols, re->rows);
                screen_.force_redraw();
                // Mark all dirty
                file_tree_.dirty_ = true;
                code_view_.dirty_ = true;
                agent_chat_.dirty_ = true;
                status_bar_.dirty_ = true;
            } else {
                handle_event(*ev);
            }
        }

        // 2. Drain agent events
        process_agent_updates();

        // 3. Render if anything changed
        if (file_tree_.is_dirty() || code_view_.is_dirty() ||
            agent_chat_.is_dirty() || status_bar_.is_dirty())
        {
            layout_and_render();
            screen_.flush();
        }
    }

    // Cleanup
    if (agent_thread_.joinable()) {
        agent_.request_stop();
        agent_thread_.join();
    }

    terminal_.show_cursor();
    terminal_.leave_alt_screen();
    terminal_.restore();
}

void IdeApp::handle_event(const InputEvent& ev) {
    auto* ke = std::get_if<KeyEvent>(&ev);
    if (!ke) return;

    // Global keybindings
    if (ke->ctrl && ke->ch == 'Q') { running_ = false; return; }

    // Tab to switch focus
    if (ke->key == KeyEvent::Tab) {
        focused_ = static_cast<Panel>((focused_ + 1) % 3);
        file_tree_.dirty_ = true;
        code_view_.dirty_ = true;
        agent_chat_.dirty_ = true;
        return;
    }

    // Dispatch to focused panel
    switch (focused_) {
        case PanelFileTree: file_tree_.on_event(ev); break;
        case PanelCode:     code_view_.on_event(ev); break;
        case PanelChat:     agent_chat_.on_event(ev); break;
    }
}

void IdeApp::process_agent_updates() {
    std::vector<UiEvent> events;
    {
        std::lock_guard lock(queue_mu_);
        events.swap(pending_);
    }

    for (auto& evt : events) {
        std::visit(Overloaded{
            [&](UiTokens& t) {
                agent_chat_.append_token(t.text);
                status_bar_.set_agent_status("streaming");
            },
            [&](UiToolQueued& t) {
                agent_chat_.add_tool(t.name, t.id);
                status_bar_.set_agent_status("tool: " + t.name);
            },
            [&](UiToolRunning& t) {
                agent_chat_.add_tool(t.name, t.id, t.summary);
                status_bar_.set_agent_status("running: " + t.name);

                // Auto-open files the agent reads
                if (t.name == "read_file" || t.name == "edit") {
                    code_view_.open_file(t.summary);
                    status_bar_.set_file(t.summary);
                }
            },
            [&](UiToolEnd& t) {
                agent_chat_.finish_tool(t.id, t.is_error, t.duration_ms);
            },
            [&](UiStatus&) {
                status_bar_.set_agent_status("thinking");
            },
            [&](UiError& e) {
                agent_chat_.add_error(e.error.message);
                status_bar_.set_agent_status("error");
            },
            [&](UiDone&) {
                agent_chat_.finish_response();
                status_bar_.set_agent_status("idle");
            },
        }, evt);
    }
}

void IdeApp::layout_and_render() {
    auto [cols, rows] = terminal_.size();
    if (cols < 10 || rows < 5) return;

    auto& buf = screen_.back();
    buf.clear();

    Rect full{0, 0, cols, rows};

    // Status bar at bottom (1 row)
    auto [body, status_rect] = full.vsplit(static_cast<uint16_t>(rows - 1));

    // Body: top = file_tree + code_view (55%), bottom = chat (45%)
    uint16_t top_h = body.height * 55 / 100;
    auto [top, chat_rect] = body.vsplit(top_h);

    // Top: file_tree (20 cols or 25%) | code_view
    uint16_t tree_w = std::min<uint16_t>(24, cols / 4);
    auto [tree_rect, code_rect] = top.hsplit(tree_w);

    // Borders
    Style border = Style{}.with_fg(Color::BrightBlack);
    buf.vline(tree_rect.width, top.y, top.height, '|', border);
    buf.hline(0, top.y + top.height, cols, '-', border);

    // Focus indicators
    auto focus_border = [&](Rect r, Panel p) {
        if (focused_ == p) {
            auto style = Style{}.with_fg(Color::Cyan);
            buf.hline(r.x, r.y, r.width, '-', style);
        }
    };
    focus_border(tree_rect, PanelFileTree);
    focus_border(code_rect, PanelCode);
    focus_border(chat_rect, PanelChat);

    // Render widgets
    Rect tree_inner = {tree_rect.x, static_cast<uint16_t>(tree_rect.y),
                       static_cast<uint16_t>(tree_rect.width - 1), tree_rect.height};
    Rect code_inner = {static_cast<uint16_t>(code_rect.x + 1), code_rect.y,
                       static_cast<uint16_t>(code_rect.width - 1), code_rect.height};
    Rect chat_inner = {chat_rect.x, static_cast<uint16_t>(chat_rect.y + 1),
                       chat_rect.width, static_cast<uint16_t>(chat_rect.height - 1)};

    file_tree_.render(tree_inner, buf);
    code_view_.render(code_inner, buf);
    agent_chat_.render(chat_inner, buf);
    status_bar_.render(status_rect, buf);

    // Show cursor in chat input when focused
    if (focused_ == PanelChat) {
        terminal_.show_cursor();
        terminal_.move_cursor(
            static_cast<uint16_t>(chat_inner.x + 2),
            static_cast<uint16_t>(chat_inner.y + chat_inner.height - 1));
    } else {
        terminal_.hide_cursor();
    }
}

void IdeApp::submit_to_agent(std::string message) {
    // Wait for previous agent run to finish
    if (agent_thread_.joinable())
        agent_thread_.join();

    status_bar_.set_agent_status("thinking");

    auto sink = build_ui_sink();
    agent_thread_ = std::jthread([this, msg = std::move(message), sink = std::move(sink)]() {
        auto result = agent_.run_turn(msg, sink);
        if (!result) {
            std::lock_guard lock(queue_mu_);
            pending_.push_back(UiError{result.error()});
        }
    });
}

auto IdeApp::build_ui_sink() -> UiSink {
    return [this](UiEvent evt) {
        std::lock_guard lock(queue_mu_);
        pending_.push_back(std::move(evt));
    };
}

} // namespace clawed::ide
