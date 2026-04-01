#include <clawed/tui/app.hpp>
#include <clawed/tui/input_view.hpp>
#include <clawed/tui/renderer.hpp>

#include <iostream>
#include <format>

namespace clawed::tui {

App::App(ApiClient& client, ToolRegistry& registry, AgentLoop::Config agent_config)
    : client_(client)
    , registry_(registry)
    , agent_(client_, registry_, std::move(agent_config))
{}

void App::run() { run_interactive(); }

namespace {

// ── ANSI helpers (raw, fast, no allocation for constants) ────────────────────

constexpr auto RST  = "\033[0m";
constexpr auto BOLD = "\033[1m";
constexpr auto DIM  = "\033[2m";
constexpr auto ITAL = "\033[3m";

constexpr auto FG_RED     = "\033[31m";
constexpr auto FG_GREEN   = "\033[32m";
constexpr auto FG_YELLOW  = "\033[33m";
constexpr auto FG_BLUE    = "\033[34m";
constexpr auto FG_MAGENTA = "\033[35m";
constexpr auto FG_CYAN    = "\033[1;36m";
constexpr auto FG_GRAY    = "\033[90m";

constexpr auto CURSOR_UP    = "\033[A";
constexpr auto CLEAR_LINE   = "\033[2K\r";

auto compact_error(const std::string& msg) -> std::string {
    std::string s;
    int lines = 0;
    for (size_t i = 0; i < msg.size() && s.size() < 120; ++i) {
        if (msg[i] == '\n' && ++lines >= 2) break;
        s += msg[i];
    }
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

} // anonymous namespace

void App::run_interactive() {
    InputView input;

    // ── Banner ───────────────────────────────────────────────────────────
    std::cout << "\n"
              << FG_CYAN << "  clawed" << RST << FG_GRAY << " v0.1.0" << RST << "\n"
              << FG_GRAY << "  C++ Claude agent runtime" << RST << "\n\n"
              << std::flush;

    while (true) {
        auto line = input.read_line(
            std::format("{}{}>{} ", BOLD, FG_MAGENTA, RST));

        if (line.empty() || line == "quit" || line == "exit") {
            std::cout << "\n" << FG_GRAY << "  Bye." << RST << "\n\n" << std::flush;
            break;
        }

        bool has_text = false;
        int  step_num = 0;

        // Tool tracking for in-place updates
        struct ToolSlot { std::string name; std::string id; bool printed = false; };
        std::vector<ToolSlot> slots;
        bool slots_printed = false;

        UiSink ui_sink = [&](UiEvent evt) {
            std::visit(Overloaded{

                // ── Streaming tokens (hot path) ──────────────────────────
                [&](UiTokens& t) {
                    std::cout << t.text << std::flush;
                    has_text = true;
                },

                // ── Tool queued (during streaming) ───────────────────────
                [&](UiToolStart& t) {
                    if (has_text) {
                        std::cout << "\n";
                        has_text = false;
                    }
                    slots.push_back({t.name, t.id});
                },

                // ── Tool completed ───────────────────────────────────────
                [&](UiToolEnd& t) {
                    // First result: print all queued tools with spinner placeholders
                    if (!slots_printed) {
                        slots_printed = true;
                        std::cout << "\n";
                        for (auto& s : slots) {
                            auto sid = s.id.size() > 8 ? s.id.substr(0, 8) : s.id;
                            std::cout << FG_GRAY << "  " << DIM << "\xe2\x97\x8b" << RST
                                      << FG_GRAY << " " << s.name
                                      << " " << DIM << sid << RST << "\n";
                        }
                        std::cout << std::flush;
                    }

                    // Find the slot index
                    int idx = -1;
                    for (int i = 0; i < static_cast<int>(slots.size()); ++i)
                        if (slots[i].id == t.id) { idx = i; break; }

                    if (idx < 0) return;

                    // Move cursor to the tool's line and rewrite it
                    int up = static_cast<int>(slots.size()) - idx;
                    for (int i = 0; i < up; ++i) std::cout << CURSOR_UP;
                    std::cout << CLEAR_LINE;

                    auto sid = t.id.size() > 8 ? t.id.substr(0, 8) : t.id;
                    if (t.is_error) {
                        auto msg = compact_error(t.result);
                        std::cout << FG_RED << "  \xe2\x9c\x97 " << slots[idx].name
                                  << " " << DIM << sid << RST
                                  << FG_RED << " " << msg << RST;
                    } else {
                        std::cout << FG_GREEN << "  \xe2\x9c\x93 " << RST
                                  << FG_GRAY << slots[idx].name
                                  << " " << DIM << sid << RST;
                    }

                    // Move cursor back down
                    std::cout << "\n";
                    for (int i = 1; i < up; ++i) std::cout << "\033[B";
                    std::cout << std::flush;
                },

                // ── Status ───────────────────────────────────────────────
                [&](UiStatus&) {
                    if (step_num == 0 && !has_text) {
                        // Only show on first step, before any output
                    }
                    // Reset tool state for next round
                    if (slots_printed) {
                        slots.clear();
                        slots_printed = false;
                        ++step_num;
                    }
                },

                // ── Error ────────────────────────────────────────────────
                [&](UiError& e) {
                    std::cout << "\n" << FG_RED << BOLD
                              << "  Error: " << RST << FG_RED
                              << e.error.message << RST << "\n" << std::flush;
                },

                // ── Done ─────────────────────────────────────────────────
                [&](UiDone&) {
                    if (has_text) std::cout << "\n";
                    std::cout << "\n" << std::flush;
                }

            }, evt);
        };

        auto result = agent_.run_turn(line, std::move(ui_sink));
        if (!result) {
            std::cout << "\n" << FG_RED << BOLD << "  Error: " << RST
                      << FG_RED << result.error().message << RST << "\n" << std::flush;
        }
    }
}

} // namespace clawed::tui
