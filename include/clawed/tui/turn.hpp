#pragma once

// ── Turn state machine ───────────────────────────────────────────────────────
//
// Models the lifecycle of a single agent turn as a state machine where
// illegal states are unrepresentable at the type level.
//
// Tool lifecycle:
//   Pending → Running → Completed | Failed
//                        ↑ has result  ↑ has error
//
// Turn phases:
//   Idle → Streaming → ToolsQueued → ToolsRunning → Streaming → ... → Done
//
// Each state carries exactly the data it needs — no more, no less.
// std::variant ensures you can only be in one state at a time.
// Transition functions return the new state type, preventing invalid transitions.

#include <clawed/tui/style.hpp>
#include <string>
#include <variant>
#include <vector>

namespace clawed::tui::turn {

// ── Tool states ──────────────────────────────────────────────────────────────
// A tool progresses: Queued → Running → Completed | Failed
// Each state carries exactly what it needs.

namespace tool {

/// Tool has been declared by the model but not yet started.
struct Queued {
    std::string name;
    std::string id;
};

/// Tool is currently executing.
struct Running {
    std::string name;
    std::string id;
};

/// Tool completed successfully.
struct Completed {
    std::string name;
    std::string id;
    std::string result;
};

/// Tool failed with an error.
struct Failed {
    std::string name;
    std::string id;
    std::string error;
};

/// A tool in any state.
using State = std::variant<Queued, Running, Completed, Failed>;

/// Get the tool ID regardless of state.
inline auto id_of(const State& s) -> const std::string& {
    return std::visit([](const auto& t) -> const std::string& { return t.id; }, s);
}

/// Get the tool name regardless of state.
inline auto name_of(const State& s) -> const std::string& {
    return std::visit([](const auto& t) -> const std::string& { return t.name; }, s);
}

/// Is this tool finished (completed or failed)?
inline auto is_done(const State& s) -> bool {
    return std::holds_alternative<Completed>(s) || std::holds_alternative<Failed>(s);
}

// ── Transitions (only valid ones compile) ────────────────────────────────

inline auto start(Queued q) -> Running {
    return Running{std::move(q.name), std::move(q.id)};
}

inline auto complete(Running r, std::string result) -> Completed {
    return Completed{std::move(r.name), std::move(r.id), std::move(result)};
}

inline auto fail(Running r, std::string error) -> Failed {
    return Failed{std::move(r.name), std::move(r.id), std::move(error)};
}

} // namespace tool

// ── Turn phases ──────────────────────────────────────────────────────────────
// The turn progresses through these phases. Each phase carries its data.

/// Waiting for model response. No output yet.
struct Thinking {};

/// Streaming text tokens from the model.
struct Streaming {
    std::string accumulated;
};

/// Model declared tool calls, waiting for execution to start.
struct ToolsQueued {
    std::string text_before;  // any text emitted before tools
    std::vector<tool::State> tools;
};

/// Tools are being executed.
struct ToolsRunning {
    std::string text_before;
    std::vector<tool::State> tools;
};

/// Turn completed.
struct Done {
    std::string text;
};

/// Turn hit an error.
struct Errored {
    std::string message;
};

/// Turn phase — only one active at a time.
using Phase = std::variant<Thinking, Streaming, ToolsQueued, ToolsRunning, Done, Errored>;

// ── Turn aggregate ───────────────────────────────────────────────────────────
// Holds the full turn state, updated by events from the agent loop.

class Turn {
public:
    Turn() : phase_(Thinking{}) {}

    // ── Event handlers (state transitions) ──

    /// Model started streaming text.
    void on_text(const std::string& token) {
        std::visit(Overloaded{
            [&](Thinking&) {
                phase_ = Streaming{token};
            },
            [&](Streaming& s) {
                s.accumulated += token;
            },
            [&](auto&) {
                // Text after tools — append to accumulated
                accumulated_text_ += token;
            }
        }, phase_);
    }

    /// Model declared a tool call (during streaming).
    void on_tool_queued(const std::string& name, const std::string& id) {
        std::visit(Overloaded{
            [&](Streaming& s) {
                ToolsQueued tq;
                tq.text_before = std::move(s.accumulated);
                tq.tools.emplace_back(tool::Queued{name, id});
                phase_ = std::move(tq);
            },
            [&](ToolsQueued& tq) {
                tq.tools.emplace_back(tool::Queued{name, id});
            },
            [&](Thinking&) {
                ToolsQueued tq;
                tq.tools.emplace_back(tool::Queued{name, id});
                phase_ = std::move(tq);
            },
            [&](auto&) {}
        }, phase_);
    }

    /// Tools are starting execution (transition Queued → Running).
    void on_tools_executing() {
        if (auto* tq = std::get_if<ToolsQueued>(&phase_)) {
            ToolsRunning tr;
            tr.text_before = std::move(tq->text_before);
            for (auto& t : tq->tools) {
                if (auto* q = std::get_if<tool::Queued>(&t)) {
                    tr.tools.emplace_back(tool::start(std::move(*q)));
                } else {
                    tr.tools.push_back(std::move(t));
                }
            }
            phase_ = std::move(tr);
        }
    }

    /// A tool completed or failed.
    void on_tool_result(const std::string& id, const std::string& output, bool is_error) {
        auto update = [&](std::vector<tool::State>& tools) {
            for (auto& t : tools) {
                if (tool::id_of(t) == id) {
                    if (auto* r = std::get_if<tool::Running>(&t)) {
                        if (is_error)
                            t = tool::fail(std::move(*r), output);
                        else
                            t = tool::complete(std::move(*r), output);
                    } else if (auto* q = std::get_if<tool::Queued>(&t)) {
                        // Directly from queued (if on_tools_executing wasn't called)
                        auto running = tool::start(std::move(*q));
                        if (is_error)
                            t = tool::fail(std::move(running), output);
                        else
                            t = tool::complete(std::move(running), output);
                    }
                    break;
                }
            }
        };

        std::visit(Overloaded{
            [&](ToolsRunning& tr) { update(tr.tools); },
            [&](ToolsQueued& tq) { update(tq.tools); },
            [&](auto&) {}
        }, phase_);
    }

    /// Turn completed.
    void on_done() {
        phase_ = Done{accumulated_text_};
    }

    /// Error occurred.
    void on_error(const std::string& msg) {
        phase_ = Errored{msg};
    }

    /// Reset for next tool round (after tools complete, model streams again).
    void on_next_round() {
        // Preserve text, reset tool state
        std::visit(Overloaded{
            [&](ToolsRunning& tr) {
                accumulated_text_ += tr.text_before;
            },
            [&](ToolsQueued& tq) {
                accumulated_text_ += tq.text_before;
            },
            [&](auto&) {}
        }, phase_);
        phase_ = Thinking{};
    }

    [[nodiscard]] auto phase() const -> const Phase& { return phase_; }

    [[nodiscard]] auto tools() const -> const std::vector<tool::State>* {
        if (auto* tr = std::get_if<ToolsRunning>(&phase_)) return &tr->tools;
        if (auto* tq = std::get_if<ToolsQueued>(&phase_)) return &tq->tools;
        return nullptr;
    }

private:
    Phase phase_;
    std::string accumulated_text_;
};

} // namespace clawed::tui::turn
