#include <clawed/tui/app.hpp>
#include <clawed/tui/input_view.hpp>

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

// ── Raw ANSI (no allocation, maximum speed) ──────────────────────────────────

constexpr auto RST   = "\033[0m";
constexpr auto BOLD  = "\033[1m";
constexpr auto DIM   = "\033[2m";

constexpr auto RED   = "\033[31m";
constexpr auto GREEN = "\033[32m";
constexpr auto YELL  = "\033[33m";
constexpr auto BLUE  = "\033[34m";
constexpr auto MAG   = "\033[35m";
constexpr auto CYAN  = "\033[1;36m";
constexpr auto GRAY  = "\033[90m";

constexpr auto CUP   = "\033[A";      // cursor up
constexpr auto CCLR  = "\033[2K\r";   // clear line

/// Format duration in human-readable form.
auto fmt_dur(int ms) -> std::string {
    if (ms < 1000) return std::format("{}ms", ms);
    return std::format("{:.1f}s", ms / 1000.0);
}

/// Truncate string to max length.
auto trunc(const std::string& s, size_t max = 50) -> std::string {
    if (s.size() <= max) return s;
    return s.substr(0, max) + "...";
}

} // anonymous namespace

void App::run_interactive() {
    InputView input;

    // ── Banner ───────────────────────────────────────────────────────────
    std::cout << "\n"
              << CYAN << "  clawed" << RST << GRAY << " v0.1.0" << RST << "\n"
              << GRAY << "  type a message, 'quit' to exit" << RST << "\n\n"
              << std::flush;

    while (true) {
        auto line = input.read_line(
            std::format("{}{}>{} ", BOLD, MAG, RST));

        if (line.empty() || line == "quit" || line == "exit") {
            std::cout << "\n" << GRAY << "  bye" << RST << "\n\n" << std::flush;
            break;
        }

        // ── Per-turn state ───────────────────────────────────────────────
        bool has_text = false;
        int  round = 0;  // tool round counter

        struct Slot {
            std::string name;
            std::string id;
            std::string summary;
            enum { Queued, Running, Done, Failed } state = Queued;
        };
        std::vector<Slot> slots;
        bool slots_printed = false;

        // Helper: print/reprint all slots from scratch
        auto print_slots = [&]() {
            if (!slots_printed) {
                std::cout << "\n";
                slots_printed = true;
            }
            // Move up to first slot line
            for (size_t i = 0; i < slots.size(); ++i)
                std::cout << CUP;

            for (auto& s : slots) {
                std::cout << CCLR;
                auto sid = s.id.size() > 8 ? s.id.substr(0, 8) : s.id;
                auto label = trunc(s.summary.empty() ? s.name : s.summary);

                switch (s.state) {
                case Slot::Queued:
                    std::cout << GRAY << "  " << DIM << "\xe2\x97\x8b" << RST  // ○
                              << GRAY << " " << s.name << RST;
                    break;
                case Slot::Running:
                    std::cout << YELL << "  \xe2\x97\x8f" << RST               // ●
                              << " " << GRAY << s.name << RST
                              << DIM << " " << label << RST;
                    break;
                case Slot::Done:
                    std::cout << GREEN << "  \xe2\x9c\x93" << RST              // ✓
                              << " " << GRAY << s.name << RST
                              << DIM << " " << label << RST;
                    break;
                case Slot::Failed:
                    std::cout << RED << "  \xe2\x9c\x97" << RST                // ✗
                              << " " << RED << s.name << RST
                              << DIM << " " << label << RST;
                    break;
                }
                std::cout << "\n";
            }
            std::cout << std::flush;
        };

        auto find_slot = [&](const std::string& id) -> Slot* {
            for (auto& s : slots)
                if (s.id == id) return &s;
            return nullptr;
        };

        // ── UI sink ──────────────────────────────────────────────────────
        UiSink ui_sink = [&](UiEvent evt) {
            std::visit(Overloaded{

                // Streaming text — hot path, direct write
                [&](UiTokens& t) {
                    std::cout << t.text << std::flush;
                    has_text = true;
                },

                // Model declared a tool call (during streaming)
                [&](UiToolQueued& t) {
                    if (has_text) {
                        std::cout << "\n";
                        has_text = false;
                    }
                    slots.push_back({t.name, t.id, {}, Slot::Queued});
                },

                // Tool is about to execute — shows WHAT it's doing
                [&](UiToolRunning& t) {
                    if (auto* s = find_slot(t.id)) {
                        s->state = Slot::Running;
                        s->summary = t.summary;
                    }
                    print_slots();
                },

                // Tool completed — shows result + timing
                [&](UiToolEnd& t) {
                    if (auto* s = find_slot(t.id)) {
                        s->state = t.is_error ? Slot::Failed : Slot::Done;
                        if (!s->summary.empty()) {
                            s->summary += std::format(" {}{}{}", DIM, fmt_dur(t.duration_ms), RST);
                        } else {
                            s->summary = fmt_dur(t.duration_ms);
                        }
                        if (t.is_error) {
                            // Append compact error
                            auto msg = t.result;
                            if (msg.size() > 60) msg = msg.substr(0, 60) + "...";
                            s->summary += std::format(" {}{}{}", RED, msg, RST);
                        }
                    }
                    print_slots();
                },

                // Status: thinking between rounds
                [&](UiStatus&) {
                    if (slots_printed) {
                        // New round — clear slot state
                        ++round;
                        slots.clear();
                        slots_printed = false;
                    }
                },

                // Error
                [&](UiError& e) {
                    std::cout << "\n" << RED << BOLD
                              << "  error: " << RST << RED
                              << e.error.message << RST << "\n" << std::flush;
                },

                // Turn complete
                [&](UiDone&) {
                    if (has_text) std::cout << "\n";
                    std::cout << "\n" << std::flush;
                }

            }, evt);
        };

        auto result = agent_.run_turn(line, std::move(ui_sink));
        if (!result) {
            std::cout << "\n" << RED << BOLD << "  error: " << RST
                      << RED << result.error().message << RST << "\n\n" << std::flush;
        }
    }
}

} // namespace clawed::tui
