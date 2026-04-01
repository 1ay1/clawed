#include <clawed/tui/app.hpp>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <format>
#include <mutex>
#include <thread>

namespace clawed::tui {

using namespace ftxui;

App::App(ApiClient& client, ToolRegistry& registry, AgentLoop::Config agent_config)
    : client_(client)
    , registry_(registry)
    , agent_(client_, registry_, std::move(agent_config))
{}

void App::run() { run_interactive(); }

// ── Conversation model ──────────────────────────────────────────────────────

namespace {

struct ToolCard {
    std::string name, id, summary, detail, result;
    enum State { Queued, Running, Done, Failed } state = Queued;
    int duration_ms = 0;
};

struct ConvBlock {
    enum Kind { User, Assistant, Thinking } kind;
    std::string text;
    std::vector<ToolCard> tools;
};

struct AppState {
    std::mutex mu;
    std::vector<ConvBlock> blocks;
    std::string streaming;
    bool is_streaming = false;
    std::string status = "idle";
    std::vector<ToolCard> current_tools; // tools for current round
    bool dirty = false;
};

// ── Render helpers ───────────────────────────────────────────────────────

auto render_tool(const ToolCard& t) -> Element {
    std::string icon;
    ftxui::Color icon_color;
    switch (t.state) {
        case ToolCard::Queued:  icon = "○"; icon_color = Color::GrayDark; break;
        case ToolCard::Running: icon = "●"; icon_color = Color::Yellow; break;
        case ToolCard::Done:    icon = "✓"; icon_color = Color::Green; break;
        case ToolCard::Failed:  icon = "✗"; icon_color = Color::Red; break;
    }

    auto label = t.summary.empty() ? t.name : t.name + " " + t.summary;
    if (label.size() > 70) label = label.substr(0, 70) + "...";

    Elements line_parts;
    line_parts.push_back(text("  " + icon + " ") | color(icon_color));
    line_parts.push_back(text(t.name + " ") | color(Color::White));

    if (!t.summary.empty() && t.summary != t.name) {
        auto s = t.summary;
        if (s.size() > 60) s = s.substr(0, 60) + "...";
        line_parts.push_back(text(s + " ") | color(Color::GrayLight));
    }

    if (t.duration_ms > 0) {
        std::string dur = t.duration_ms < 1000
            ? std::format("{}ms", t.duration_ms)
            : std::format("{:.1f}s", t.duration_ms / 1000.0);
        line_parts.push_back(text(dur) | color(Color::GrayDark));
    }

    Elements result;
    result.push_back(hbox(std::move(line_parts)));

    // Show detail (diff for edit, command for bash)
    if (!t.detail.empty() && (t.state == ToolCard::Running || t.state == ToolCard::Done || t.state == ToolCard::Failed)) {
        // Parse diff lines
        Elements detail_lines;
        std::istringstream iss(t.detail);
        std::string dl;
        int line_count = 0;
        while (std::getline(iss, dl) && line_count < 8) {
            if (dl.starts_with("- "))
                detail_lines.push_back(text("    " + dl) | color(Color::Red));
            else if (dl.starts_with("+ "))
                detail_lines.push_back(text("    " + dl) | color(Color::Green));
            else
                detail_lines.push_back(text("    " + dl) | color(Color::GrayLight));
            ++line_count;
        }
        if (!detail_lines.empty()) {
            result.push_back(vbox(std::move(detail_lines)));
        }
    }

    // Show error result inline
    if (t.state == ToolCard::Failed && !t.result.empty()) {
        auto msg = t.result;
        if (msg.size() > 80) msg = msg.substr(0, 80) + "...";
        result.push_back(text("    " + msg) | color(Color::Red));
    }

    return vbox(std::move(result));
}

auto render_block(const ConvBlock& block) -> Element {
    Elements parts;

    switch (block.kind) {
        case ConvBlock::User:
            parts.push_back(hbox({
                text(" > ") | color(Color::Magenta) | bold,
                paragraph(block.text) | color(Color::White),
            }));
            break;

        case ConvBlock::Thinking:
            parts.push_back(text("  ◌ " + block.text) | color(Color::GrayDark));
            break;

        case ConvBlock::Assistant:
            if (!block.text.empty())
                parts.push_back(paragraph(block.text));
            break;
    }

    for (auto& tool : block.tools)
        parts.push_back(render_tool(tool));

    parts.push_back(text(""));
    return vbox(std::move(parts));
}

} // anonymous namespace

void App::run_interactive() {
    auto screen = ScreenInteractive::Fullscreen();

    AppState state;
    std::string input_text;
    std::jthread agent_thread;

    // ── UI Sink (called from agent thread) ───────────────────────────────
    auto ui_sink = [&](UiEvent evt) {
        std::visit(Overloaded{
            [&](UiTokens& t) {
                std::lock_guard lock(state.mu);
                state.streaming += t.text;
                state.is_streaming = true;
                state.dirty = true;
            },
            [&](UiToolQueued& t) {
                std::lock_guard lock(state.mu);
                state.current_tools.push_back({
                    .name = t.name, .id = t.id, .summary = {}, .detail = {},
                    .result = {}, .state = ToolCard::Queued});
                state.dirty = true;
            },
            [&](UiToolRunning& t) {
                std::lock_guard lock(state.mu);
                for (auto& tool : state.current_tools) {
                    if (tool.id == t.id) {
                        tool.state = ToolCard::Running;
                        tool.summary = t.summary;
                        tool.detail = t.detail;
                        break;
                    }
                }
                state.status = t.name;
                state.dirty = true;
            },
            [&](UiToolEnd& t) {
                std::lock_guard lock(state.mu);
                for (auto& tool : state.current_tools) {
                    if (tool.id == t.id) {
                        tool.state = t.is_error ? ToolCard::Failed : ToolCard::Done;
                        tool.duration_ms = t.duration_ms;
                        tool.result = t.result;
                        break;
                    }
                }
                state.dirty = true;
            },
            [&](UiStatus&) {
                std::lock_guard lock(state.mu);
                // Flush current tools + streaming into a block
                if (!state.current_tools.empty() || !state.streaming.empty()) {
                    ConvBlock blk{ConvBlock::Assistant, std::move(state.streaming),
                                  std::move(state.current_tools)};
                    state.blocks.push_back(std::move(blk));
                    state.streaming.clear();
                    state.current_tools.clear();
                }
                state.status = "thinking";
                state.dirty = true;
            },
            [&](UiError& e) {
                std::lock_guard lock(state.mu);
                ConvBlock blk{ConvBlock::Assistant, "error: " + e.error.message, {}};
                state.blocks.push_back(std::move(blk));
                state.status = "error";
                state.dirty = true;
            },
            [&](UiDone&) {
                std::lock_guard lock(state.mu);
                // Flush remaining
                ConvBlock blk{ConvBlock::Assistant, std::move(state.streaming),
                              std::move(state.current_tools)};
                if (!blk.text.empty() || !blk.tools.empty())
                    state.blocks.push_back(std::move(blk));
                state.streaming.clear();
                state.current_tools.clear();
                state.is_streaming = false;
                state.status = "idle";
                state.dirty = true;
            },
        }, evt);
        screen.PostEvent(Event::Custom);
    };

    // ── Submit handler ───────────────────────────────────────────────────
    auto do_submit = [&]() {
        if (input_text.empty()) return;
        auto msg = input_text;
        input_text.clear();

        {
            std::lock_guard lock(state.mu);
            state.blocks.push_back({ConvBlock::User, msg, {}});
            state.status = "thinking";
        }

        if (agent_thread.joinable()) agent_thread.join();

        agent_thread = std::jthread([&, msg = std::move(msg), sink = ui_sink]() {
            auto result = agent_.run_turn(msg, sink);
            if (!result) sink(UiError{result.error()});
        });
    };

    // ── FTXUI Component ──────────────────────────────────────────────────
    auto component = Renderer([&] {
        std::lock_guard lock(state.mu);

        // ── Conversation ─────────────────────────────────────────────
        Elements conv_items;
        for (auto& block : state.blocks)
            conv_items.push_back(render_block(block));

        // Live streaming + current tools
        if (state.is_streaming || !state.current_tools.empty()) {
            Elements live;
            for (auto& t : state.current_tools)
                live.push_back(render_tool(t));
            if (!state.streaming.empty())
                live.push_back(paragraph(state.streaming));
            conv_items.push_back(vbox(std::move(live)));
        }

        // Status indicator (thinking)
        if (state.status == "thinking" && !state.is_streaming && state.current_tools.empty()) {
            conv_items.push_back(text("  ◌ thinking...") | color(Color::GrayDark));
        }

        auto conversation = vbox(std::move(conv_items)) |
            vscroll_indicator | focusPositionRelative(0, 1) | frame | flex;

        // ── Input ────────────────────────────────────────────────────
        auto input_line = hbox({
            text(" > ") | color(Color::Magenta) | bold,
            text(input_text.empty() ? " " : input_text),
            text("█") | color(Color::White) | blink,
        });

        // ── Status bar ───────────────────────────────────────────────
        auto status_color = state.status == "idle" ? Color::Green
                          : state.status == "error" ? Color::Red
                          : Color::Yellow;
        auto status_bar = hbox({
            text(" clawed ") | color(Color::Cyan) | bold,
            filler(),
            text(" " + state.status + " ") | color(status_color),
        }) | inverted;

        // ── Layout ───────────────────────────────────────────────────
        return vbox({
            conversation,
            separator() | color(Color::GrayDark),
            input_line,
            status_bar,
        });
    });

    // ── Event handling ───────────────────────────────────────────────────
    component = CatchEvent(component, [&](Event event) -> bool {
        // Ctrl+Q / Ctrl+C quit
        if (event.input() == std::string(1, 17) ||  // Ctrl+Q
            event.input() == std::string(1, 3)) {   // Ctrl+C
            if (agent_thread.joinable()) {
                agent_.request_stop();
                agent_thread.join();
            }
            screen.Exit();
            return true;
        }

        // Enter → submit
        if (event == Event::Return) {
            do_submit();
            return true;
        }

        // Typing
        if (event.is_character()) {
            input_text += event.character();
            return true;
        }

        // Backspace
        if (event == Event::Backspace && !input_text.empty()) {
            input_text.pop_back();
            return true;
        }

        return false;
    });

    screen.Loop(component);

    if (agent_thread.joinable()) {
        agent_.request_stop();
        agent_thread.join();
    }
}

} // namespace clawed::tui
