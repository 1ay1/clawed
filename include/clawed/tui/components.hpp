#pragma once

// ── TUI Components ──────────────────────────────────────────────────────────
//
// Each component satisfies the Component concept:
//   concept Component = requires(const T& t, Terminal& term) {
//       { t.render(term) } -> std::same_as<void>;
//   };
//
// Components write directly to Terminal — no intermediate element tree.
// This is the fastest possible path: concept dispatch → write() syscall.
//
// Available components:
//   HeaderView    — app name + model
//   ToolCardView  — tool lifecycle (queued → running → done/failed) with details
//   SpinnerView   — animated thinking/running indicator
//   PromptView    — input prompt with cursor
//   ErrorView     — styled error message
//   SeparatorView — horizontal rule

#include <clawed/tui/ansi.hpp>
#include <clawed/tui/terminal.hpp>

#include <chrono>
#include <cstdint>
#include <format>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace clawed::tui {

// ── Component concept ───────────────────────────────────────────────────────

template <typename T>
concept Component = requires(const T& t, Terminal& term) {
    { t.render(term) } -> std::same_as<void>;
};

// ── Tool state ──────────────────────────────────────────────────────────────

enum class ToolStatus : uint8_t { Queued, Running, Done, Failed };

struct ToolCardProps {
    std::string name;
    std::string id;
    std::string summary;
    std::string detail;         // bash command, edit diff
    std::string error_msg;
    std::string result_preview; // first few lines of output
    int duration_ms = 0;
    ToolStatus  status = ToolStatus::Queued;
};

// ── HeaderView ──────────────────────────────────────────────────────────────

struct HeaderView {
    std::string_view model_name;
    std::string_view cwd;
    std::string_view git_branch;  // empty if not in a git repo

    void render(Terminal& term) const {
        using namespace ansi;
        term.writeln();
        term.writeln(std::format("  {}{}clawed{}  {}{}{}",
                                  BOLD, C, RST, DIM, model_name, RST));

        // Working directory + git branch
        std::string context = std::format("  {}{}", GR, cwd);
        if (!git_branch.empty())
            context += std::format("  {}({})", GL, git_branch);
        context += RST;
        term.writeln(context);

        auto w = std::max(10, static_cast<int>(term.width()) - 4);
        term.writeln(std::format("  {}{}{}", GR,
            std::string(static_cast<size_t>(w), '-'), RST));
        term.writeln();
    }
};
static_assert(Component<HeaderView>);

// ── SeparatorView ───────────────────────────────────────────────────────────

struct SeparatorView {
    void render(Terminal& term) const {
        using namespace ansi;
        auto w = std::max(10, static_cast<int>(term.width()) - 4);
        term.writeln(std::format("  {}{}{}", GR,
            std::string(static_cast<size_t>(w), '-'), RST));
    }
};
static_assert(Component<SeparatorView>);

// ── ToolCardView ────────────────────────────────────────────────────────────
// Shows what happened clearly:
//   edit:       ✎ file.cpp  with inline diff (- old / + new)
//   bash:       $ command    with output preview
//   read/glob/grep: compact single line
//   failed:     ✗ with error message

struct ToolCardView {
    const ToolCardProps& props;

    void render(Terminal& term) const {
        using namespace ansi;

        // Status indicator
        const char* si; const char* sc;
        switch (props.status) {
            case ToolStatus::Queued:  si = "\xe2\x97\xa6"; sc = GR; break; // ◦
            case ToolStatus::Running: si = "\xe2\x9f\xb3"; sc = Y;  break; // ⟳
            case ToolStatus::Done:    si = "\xe2\x9c\x93"; sc = G;  break; // ✓
            case ToolStatus::Failed:  si = "\xe2\x9c\x97"; sc = R;  break; // ✗
        }

        auto summary = props.summary;
        auto max_w = static_cast<size_t>(std::max(40, term.width() - 25));
        if (summary.size() > max_w)
            summary = summary.substr(0, max_w) + "\xe2\x80\xa6";

        // Duration string
        std::string dur;
        if (props.duration_ms > 0) {
            dur = props.duration_ms < 1000
                ? std::format(" {}{}ms{}", DIM, props.duration_ms, RST)
                : std::format(" {}{:.1f}s{}", DIM, props.duration_ms / 1000.0, RST);
        }

        // ── Edit tool: show file + diff ─────────────────────────────────────
        if (props.name == "edit") {
            term.writeln(std::format("  {}{}{} {}\xe2\x9c\x8e{} {}{}{}{}",
                sc, si, RST, GR, RST, W, summary, RST, dur));

            // Show diff lines
            if (!props.detail.empty() && props.status != ToolStatus::Queued) {
                std::istringstream iss(props.detail);
                std::string dl;
                int n = 0;
                while (std::getline(iss, dl) && n++ < 10) {
                    if (dl.size() > 90) dl = dl.substr(0, 90) + "\xe2\x80\xa6";
                    const char* lc = GR;
                    if (dl.starts_with("- ")) lc = R;
                    else if (dl.starts_with("+ ")) lc = G;
                    term.writeln(std::format("    {}\xe2\x94\x82{} {}{}{}", GR, RST, lc, dl, RST));
                }
            }
            return;
        }

        // ── Bash tool: show command + output preview ────────────────────────
        if (props.name == "bash") {
            term.writeln(std::format("  {}{}{} {}${} {}{}{}{}",
                sc, si, RST, GR, RST, GL, summary, RST, dur));

            // Show output preview (first few lines)
            if (!props.result_preview.empty() && props.status == ToolStatus::Done) {
                std::istringstream iss(props.result_preview);
                std::string rl;
                int n = 0;
                while (std::getline(iss, rl) && n++ < 4) {
                    if (rl.size() > 90) rl = rl.substr(0, 90) + "\xe2\x80\xa6";
                    term.writeln(std::format("    {}\xe2\x94\x82{} {}{}{}", GR, RST, GR, rl, RST));
                }
            }
        }

        // ── Write tool: show file path ──────────────────────────────────────
        else if (props.name == "write_file") {
            term.writeln(std::format("  {}{}{} {}\xe2\x86\x90{} {}wrote {}{}{}",
                sc, si, RST, GR, RST, DIM, RST, summary, dur));
        }

        // ── Read/glob/grep: compact single line ─────────────────────────────
        else {
            const char* icon = "\xe2\x94\x80";
            if (props.name == "read_file") icon = "\xe2\x86\x92";      // →
            else if (props.name == "grep") icon = "\xe2\x9c\xa6";      // ✦
            else if (props.name == "glob") icon = "\xe2\x9c\xa6";      // ✦

            term.writeln(std::format("  {}{}{} {}{}{} {}{} {}{}{}",
                sc, si, RST, GR, icon, RST, C, props.name, GR, summary, dur));
        }

        // ── Error (any tool) ────────────────────────────────────────────────
        if (props.status == ToolStatus::Failed && !props.error_msg.empty()) {
            auto msg = props.error_msg;
            if (msg.size() > 90) msg = msg.substr(0, 90) + "\xe2\x80\xa6";
            term.writeln(std::format("    {}error: {}{}", R, msg, RST));
        }
    }
};
static_assert(Component<ToolCardView>);

// ── SpinnerView ─────────────────────────────────────────────────────────────
// Animated braille spinner with context: thinking/running tool, elapsed time, round.

struct SpinnerView {
    int64_t elapsed_ms = 0;

    void render(Terminal& term) const {
        using namespace ansi;

        static const char* frames[] = {
            "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8",
            "\xe2\xa0\xbc", "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7",
            "\xe2\xa0\x87", "\xe2\xa0\x8f"
        };
        auto frame = frames[(elapsed_ms / 80) % 10];

        std::string line = std::format("  {}{}{}", GR, frame, RST);

        // Only show elapsed after 3 seconds (like Zed's 30s but shorter for CLI)
        if (elapsed_ms >= 3000) {
            auto secs = elapsed_ms / 1000;
            line += std::format("  {}{}s{}", DIM, secs, RST);
        }

        term.writeln(line);
    }
};
static_assert(Component<SpinnerView>);

// ── PromptView ──────────────────────────────────────────────────────────────

struct PromptView {
    std::string_view input;
    bool active = true;

    void render(Terminal& term) const {
        using namespace ansi;
        term.clear_line();
        if (active)
            term.write(std::format("  {}\xe2\x9d\xaf{} {}", B, RST, input));
        else
            term.write(std::format("  {}\xe2\x9d\xaf{} {}{}{}", GR, RST, DIM, input, RST));
    }
};
static_assert(Component<PromptView>);

// ── ErrorView ───────────────────────────────────────────────────────────────

struct ErrorView {
    std::string_view message;

    void render(Terminal& term) const {
        using namespace ansi;
        term.writeln();
        term.writeln(std::format("  {}{}\xe2\x9c\x97 {}{}", BOLD, R, message, RST));
    }
};
static_assert(Component<ErrorView>);

// ── UserMessageView ─────────────────────────────────────────────────────────

struct UserMessageView {
    std::string_view message;

    void render(Terminal& term) const {
        using namespace ansi;
        term.clear_line();
        term.writeln(std::format("  {}\xe2\x9d\xaf{} {}{}{}",
                                  B, RST, BOLD, message, RST));
    }
};
static_assert(Component<UserMessageView>);

// ── TimingView ──────────────────────────────────────────────────────────────
// Shown after turn completion: "* Response took Xs  (N tools, M rounds)"

struct TimingView {
    int64_t elapsed_ms = 0;
    int tools_used = 0;
    int api_rounds = 0;

    void render(Terminal& term) const {
        using namespace ansi;
        std::string line = std::format("  {}\xe2\x9c\xbb ", GR);  // ✻

        if (elapsed_ms < 1000)
            line += std::format("{}ms", elapsed_ms);
        else if (elapsed_ms < 60000)
            line += std::format("{:.1f}s", elapsed_ms / 1000.0);
        else {
            auto mins = elapsed_ms / 60000;
            auto secs = (elapsed_ms % 60000) / 1000;
            line += std::format("{}m {}s", mins, secs);
        }

        if (tools_used > 0)
            line += std::format("  \xc2\xb7  {} tool{}", tools_used,
                                tools_used == 1 ? "" : "s");
        if (api_rounds > 1)
            line += std::format("  \xc2\xb7  {} rounds", api_rounds);

        line += RST;
        term.writeln(line);
    }
};
static_assert(Component<TimingView>);

// ── StatusBarView ───────────────────────────────────────────────────────────
// Persistent bar at the bottom: model, git branch, status indicator

struct StatusBarView {
    std::string_view model;
    std::string_view git_branch;
    int turn_count = 0;

    void render(Terminal& term) const {
        using namespace ansi;
        auto w = term.width();

        // Right side: git branch + model
        std::string right;
        if (!git_branch.empty())
            right = std::format("{}{}", git_branch, RST);

        // Left side: turn info
        std::string left;
        if (turn_count > 0)
            left = std::format("{}{} turn{}", GR, turn_count, turn_count == 1 ? "" : "s");
        else
            left = std::format("{}ready", GR);

        // Build the bar
        term.clear_line();
        term.write(std::format("  {}  ", left));

        // Fill space (approximate — ANSI codes make exact width tricky)
        auto visible_left = static_cast<int>(left.size()) - 4;  // subtract ANSI
        auto visible_right = static_cast<int>(right.size()) - 4;
        auto padding = std::max(1, w - visible_left - visible_right - 6);
        for (int i = 0; i < padding; ++i) term.write(' ');

        if (!right.empty())
            term.write(std::format("{}{}  ", GR, right));
        term.write(RST);
    }
};
static_assert(Component<StatusBarView>);

} // namespace clawed::tui
