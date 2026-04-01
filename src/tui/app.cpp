#include <clawed/tui/app.hpp>
#include <clawed/tui/input_view.hpp>
#include <clawed/tui/renderer.hpp>
#include <clawed/tui/components.hpp>

#include <iostream>
#include <format>

namespace clawed::tui {

App::App(ApiClient& client, ToolRegistry& registry, AgentLoop::Config agent_config)
    : client_(client)
    , registry_(registry)
    , agent_(client_, registry_, std::move(agent_config))
{}

void App::run() {
    run_interactive();
}

namespace {

/// Summarize tool input for compact display.
auto summarize_tool(const std::string& name, const std::string& id) -> std::string {
    auto short_id = id.size() > 8 ? id.substr(0, 8) : id;
    return std::format("{} [{}]", name, short_id);
}

/// Compact result: first 3 lines, max 200 chars.
auto compact_result(const std::string& result, bool is_error) -> std::string {
    if (result.empty()) return is_error ? "error" : "done";

    std::string compact;
    int lines = 0;
    for (size_t i = 0; i < result.size() && lines < 3 && compact.size() < 200; ++i) {
        if (result[i] == '\n') {
            ++lines;
            if (lines >= 3) break;
        }
        compact += result[i];
    }

    // Trim trailing whitespace
    while (!compact.empty() && (compact.back() == '\n' || compact.back() == ' '))
        compact.pop_back();

    if (compact.size() < result.size())
        compact += " ...";

    return compact;
}

} // anonymous namespace

void App::run_interactive() {
    InputView input;
    Renderer renderer;

    // Clean banner
    renderer.stream_styled("clawed", styles::heading);
    renderer.stream_styled(" v0.1.0\n", styles::muted);
    renderer.flush();

    while (true) {
        auto line = input.read_line("\033[1;32m> \033[0m");

        if (line.empty() || line == "quit" || line == "exit") {
            renderer.newline();
            break;
        }

        renderer.reset();
        bool has_streamed_text = false;
        int  tool_count = 0;

        // Track tool IDs in order so we can pair start/end
        struct PendingTool { std::string name; std::string id; };
        std::vector<PendingTool> pending_tools;
        bool all_starts_done = false;

        UiSink ui_sink = [&](UiEvent evt) {
            std::visit(Overloaded{
                [&](UiTokens& t) {
                    renderer.stream_token(t.text);
                    renderer.flush();
                    has_streamed_text = true;
                },

                [&](UiToolStart& t) {
                    ++tool_count;
                    if (has_streamed_text) {
                        renderer.newline();
                        has_streamed_text = false;
                    }
                    pending_tools.push_back({t.name, t.id});
                    // Don't print yet — wait until all starts are collected
                    // (they all fire during streaming before any tool executes)
                },

                [&](UiToolEnd& t) {
                    // On first ToolEnd, print all pending tool lines
                    if (!all_starts_done) {
                        all_starts_done = true;
                        for (auto& pt : pending_tools) {
                            renderer.stream_styled(
                                std::format("  {} ", summarize_tool(pt.name, pt.id)),
                                styles::muted);
                            renderer.newline();
                        }
                    }

                    // Find and update the matching tool
                    auto short_id = t.id.size() > 8 ? t.id.substr(0, 8) : t.id;

                    // Move cursor up to the right line and overwrite
                    // Find index of this tool
                    int idx = -1;
                    for (int i = 0; i < static_cast<int>(pending_tools.size()); ++i) {
                        if (pending_tools[i].id == t.id) { idx = i; break; }
                    }

                    if (idx >= 0) {
                        // Move up to the tool's line
                        int lines_up = static_cast<int>(pending_tools.size()) - idx;
                        if (lines_up > 0) {
                            renderer.write_raw(std::format("\033[{}A\r\033[2K", lines_up));
                        }

                        // Rewrite the line with result
                        auto label = summarize_tool(pending_tools[idx].name, short_id);
                        if (t.is_error) {
                            auto msg = compact_result(t.result, true);
                            renderer.stream_styled(std::format("  {} ", label), styles::muted);
                            renderer.stream_styled(msg, styles::error);
                        } else {
                            renderer.stream_styled(std::format("  {} ", label), styles::muted);
                            renderer.stream_styled("done", {Style{}.with_fg(Color::Green)});
                        }

                        // Move back down
                        if (lines_up > 0) {
                            renderer.write_raw(std::format("\n\033[{}B", lines_up - 1));
                        } else {
                            renderer.write_raw("\n");
                        }
                        renderer.flush();
                    }
                },

                [&](UiStatus& s) {
                    if (tool_count == 0 && !has_streamed_text) {
                        renderer.stream_styled(s.message, styles::muted);
                        renderer.newline();
                    }
                    // After tool round, show "Thinking..." again
                    if (all_starts_done) {
                        all_starts_done = false;
                        pending_tools.clear();
                        tool_count = 0;
                    }
                },

                [&](UiError& e) {
                    renderer.stream_styled(
                        "Error: " + e.error.formatted(), styles::error);
                    renderer.newline();
                },

                [&](UiDone&) {
                    if (has_streamed_text) renderer.newline();
                }
            }, evt);
        };

        auto result = agent_.run_turn(line, std::move(ui_sink));
        if (!result) {
            renderer.stream_styled(
                "Error: " + result.error().formatted(), styles::error);
            renderer.newline();
        }

        renderer.newline();
    }
}

} // namespace clawed::tui
