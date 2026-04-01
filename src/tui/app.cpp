#include <clawed/tui/app.hpp>
#include <clawed/tui/terminal.hpp>
#include <clawed/tui/md_renderer.hpp>
#include <clawed/tui/components.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace clawed::tui {

App::App(ApiClient& client, ToolRegistry& registry, AgentLoop::Config agent_config)
    : client_(client)
    , registry_(registry)
    , agent_(client_, registry_, std::move(agent_config))
{}

void App::run() { run_interactive(); }

// ─────────────────────────────────────────────────────────────────────────────
// Shared state — protected by mu. Both agent thread and UI thread take the
// lock, but only briefly. Tokens write DIRECTLY to terminal under lock —
// no buffering, no polling delay.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct State {
    std::mutex mu;

    std::string status = "idle";
    std::chrono::steady_clock::time_point thinking_since;
    std::chrono::steady_clock::time_point turn_started;  // for total turn timing

    std::vector<ToolCardProps> live_tools;

    int overlay_lines = 0;
    int turn_count = 0;
    int api_round = 0;
    int tools_used = 0;          // total tools this turn (across rounds)
    bool had_streaming = false;
    bool done = false;
    std::string error_msg;
};

auto make_preview(const std::string& name, const std::string& result) -> std::string {
    if (result.empty() || name == "write_file") return {};
    std::istringstream iss(result);
    std::string line, preview;
    int n = 0;
    while (std::getline(iss, line) && n < 3) {
        if (line.empty()) continue;
        if (line.size() > 90) line = line.substr(0, 90) + "...";
        if (!preview.empty()) preview += "\n";
        preview += line;
        n++;
    }
    int remaining = 0;
    while (std::getline(iss, line)) remaining++;
    if (remaining > 0)
        preview += std::format("\n... +{} more lines", remaining);
    return preview;
}

void clear_overlay(State& s, Terminal& term) {
    if (s.overlay_lines <= 0) return;
    for (int i = 0; i < s.overlay_lines; ++i) {
        term.clear_line();
        term.move_up();
    }
    term.clear_line();
    s.overlay_lines = 0;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────

void App::run_interactive() {
    Terminal term;
    term.enable_raw();

    MdRenderer md;

    State state;
    std::string model;
    if (auto* m = std::getenv("CLAWED_MODEL")) model = m;
    else model = "claude-opus-4-6";

    std::string input_text;
    std::jthread agent_thread;
    bool running = true;

    // Detect cwd and git branch for header
    auto cwd = std::filesystem::current_path().string();
    std::string git_branch;
    {
        // Fast git branch detection — read .git/HEAD directly
        auto git_head = std::filesystem::current_path() / ".git" / "HEAD";
        if (std::filesystem::exists(git_head)) {
            std::ifstream f(git_head);
            std::string line;
            if (std::getline(f, line)) {
                constexpr auto prefix = std::string_view("ref: refs/heads/");
                if (line.starts_with(prefix))
                    git_branch = line.substr(prefix.size());
                else if (line.size() >= 8)
                    git_branch = line.substr(0, 8); // detached HEAD
            }
        }
    }

    // Header
    HeaderView{model, cwd, git_branch}.render(term);
    PromptView{input_text, true}.render(term);
    term.flush();

    // ── Tool render tracking ────────────────────────────────────────────────
    struct ToolRS { bool running_done = false; bool completion_done = false; };
    std::vector<ToolRS> tool_rs;

    // ── UI Sink ─────────────────────────────────────────────────────────────
    // Called from agent thread. Tokens write DIRECTLY to terminal under lock.
    // No buffer. No delay. Instant.
    auto ui_sink = [&](UiEvent evt) {
        std::visit(Overloaded{
            [&](UiTokens& t) {
                // HOT PATH — write directly to terminal, zero latency
                std::lock_guard lock(state.mu);
                clear_overlay(state, term);

                if (!state.had_streaming) {
                    // First text — check if tools preceded
                    bool had_tools = false;
                    for (auto& tl : state.live_tools)
                        if (tl.status != ToolStatus::Queued) { had_tools = true; break; }
                    md.set_had_tools(had_tools);
                    state.had_streaming = true;
                }

                md.feed(t.text, term);
            },
            [&](UiToolQueued& t) {
                std::lock_guard lock(state.mu);
                state.live_tools.push_back({
                    .name = t.name, .id = t.id,
                    .summary = {}, .detail = {},
                    .error_msg = {}, .result_preview = {},
                    .duration_ms = 0, .status = ToolStatus::Queued});
            },
            [&](UiToolRunning& t) {
                std::lock_guard lock(state.mu);
                for (auto& tool : state.live_tools)
                    if (tool.id == t.id) {
                        tool.status = ToolStatus::Running;
                        tool.summary = t.summary;
                        tool.detail = t.detail;
                        break;
                    }
                state.status = t.name;
                state.thinking_since = std::chrono::steady_clock::now();
            },
            [&](UiToolEnd& t) {
                std::lock_guard lock(state.mu);
                for (auto& tool : state.live_tools)
                    if (tool.id == t.id) {
                        tool.status = t.is_error ? ToolStatus::Failed : ToolStatus::Done;
                        tool.duration_ms = t.duration_ms;
                        if (t.is_error)
                            tool.error_msg = t.result;
                        else
                            tool.result_preview = make_preview(tool.name, t.result);
                        break;
                    }
                state.tools_used++;
            },
            [&](UiStatus&) {
                std::lock_guard lock(state.mu);
                state.live_tools.clear();
                state.had_streaming = false;
                state.status = "thinking";
                state.thinking_since = std::chrono::steady_clock::now();
                state.api_round++;
                md.reset();
            },
            [&](UiError& e) {
                std::lock_guard lock(state.mu);
                state.error_msg = e.error.message;
                state.status = "error";
                state.done = true;
            },
            [&](UiDone&) {
                std::lock_guard lock(state.mu);
                state.status = "idle";
                state.done = true;
                state.turn_count++;
            },
        }, evt);
    };

    // ── Submit ──────────────────────────────────────────────────────────────
    auto do_submit = [&]() {
        if (input_text.empty()) return;
        auto msg = input_text;
        input_text.clear();

        UserMessageView{msg}.render(term);
        term.flush();

        {
            std::lock_guard lock(state.mu);
            state.status = "thinking";
            state.thinking_since = std::chrono::steady_clock::now();
            state.turn_started = state.thinking_since;
            state.live_tools.clear();
            state.had_streaming = false;
            state.done = false;
            state.error_msg.clear();
            state.api_round = 0;
            state.tools_used = 0;
            state.overlay_lines = 0;
        }

        md.reset();
        tool_rs.clear();

        if (agent_thread.joinable()) agent_thread.join();

        agent_thread = std::jthread([&, msg, sink = ui_sink]() {
            auto result = agent_.run_turn(msg, sink);
            if (!result) sink(UiError{result.error()});
        });
    };

    // ── Main loop ───────────────────────────────────────────────────────────
    // Only handles: input, tool card rendering, spinner, completion.
    // Token streaming is handled directly in ui_sink above.
    while (running) {
        auto evt = term.poll_input();
        bool idle;
        {
            std::lock_guard lock(state.mu);
            idle = (state.status == "idle" || state.status == "error");
        }

        switch (evt.type) {
            case InputType::CtrlC:
                running = false;
                continue;
            case InputType::Enter:
                if (idle) {
                    clear_overlay(state, term);
                    do_submit();
                }
                break;
            case InputType::Backspace:
                if (idle && !input_text.empty()) {
                    input_text.pop_back();
                    PromptView{input_text, true}.render(term);
                    term.flush();
                }
                break;
            case InputType::CtrlU:
                if (idle) {
                    input_text.clear();
                    PromptView{input_text, true}.render(term);
                    term.flush();
                }
                break;
            case InputType::Char:
                if (idle) {
                    input_text += evt.ch;
                    PromptView{input_text, true}.render(term);
                    term.flush();
                }
                break;
            default:
                break;
        }

        // ── Tool rendering + spinner + completion ───────────────────────────
        {
            std::lock_guard lock(state.mu);

            while (tool_rs.size() < state.live_tools.size())
                tool_rs.push_back({});

            for (size_t i = 0; i < state.live_tools.size(); ++i) {
                auto& t = state.live_tools[i];
                auto& rs = tool_rs[i];

                if (t.status == ToolStatus::Running && !rs.running_done) {
                    clear_overlay(state, term);
                    // Flush any partial text before tool output
                    if (md.has_partial()) { md.flush(term); term.writeln(); }
                    ToolCardView{t}.render(term);
                    rs.running_done = true;
                }
                if ((t.status == ToolStatus::Done || t.status == ToolStatus::Failed)
                    && !rs.completion_done) {
                    clear_overlay(state, term);
                    if (md.has_partial()) { md.flush(term); term.writeln(); }
                    ToolCardView{t}.render(term);
                    rs.running_done = true;
                    rs.completion_done = true;
                }
            }

            // Completion
            if (state.done) {
                clear_overlay(state, term);
                md.flush(term);
                if (state.had_streaming) term.writeln();

                if (!state.error_msg.empty())
                    ErrorView{state.error_msg}.render(term);

                // Timing summary
                auto turn_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - state.turn_started).count();
                TimingView{
                    .elapsed_ms = turn_ms,
                    .tools_used = state.tools_used,
                    .api_rounds = state.api_round
                }.render(term);

                term.writeln();

                state.done = false;
                state.live_tools.clear();
                state.had_streaming = false;
                tool_rs.clear();
                md.reset();

                PromptView{input_text, true}.render(term);
                continue;
            }

            // Spinner — only when no partial line on screen
            if (!idle && !md.has_partial()) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - state.thinking_since).count();

                clear_overlay(state, term);
                SpinnerView{.elapsed_ms = ms}.render(term);
                state.overlay_lines = 1;
            }

            term.flush();
        }

        auto sleep_ms = idle ? 50 : 16;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    if (agent_thread.joinable()) {
        agent_.request_stop();
        agent_thread.join();
    }

    term.writeln();
    term.disable_raw();
}

} // namespace clawed::tui
