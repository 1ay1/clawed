#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <clawed/core/states.hpp>
#include <clawed/core/events.hpp>
#include <concepts>
#include <string>
#include <variant>

namespace clawed {

// ── UI event types ───────────────────────────────────────────────────────────

struct UiTokens      { std::string text; };
struct UiToolQueued  { std::string name; std::string id; };
struct UiToolRunning {
    std::string name;
    std::string id;
    std::string summary;
    // For edit tool: carries old/new text for diff display
    std::string detail;
};
struct UiToolEnd     { std::string id; std::string result; bool is_error; int duration_ms; };
struct UiStatus      { std::string message; };
struct UiError       { Error error; };
struct UiDone        {};

using UiEvent = std::variant<UiTokens, UiToolQueued, UiToolRunning,
                             UiToolEnd, UiStatus, UiError, UiDone>;

// Type-erased sink for module boundaries (app → agent loop).
using UiSink = std::function<void(UiEvent)>;

// ── Concept: anything callable with a UiEvent ────────────────────────────────

template <typename F>
concept UiSinkConcept = std::invocable<F, UiEvent>;

// ── State machine ────────────────────────────────────────────────────────────
// State transitions are a compile-time 2D dispatch over (State × Event).
// process() is templated on the sink — each call site gets a monomorphic
// instantiation with no virtual dispatch or std::function indirection.

class StateMachine {
public:
    StateMachine() : state_(state::Idle{}) {}

    template <UiSinkConcept Sink>
    auto process(event::Event evt, Sink& ui) -> Result<void>;

    void reset() { state_ = state::Idle{}; }

    [[nodiscard]] auto current() const -> const state::State& { return state_; }

    template <typename S>
    [[nodiscard]] auto is() const -> bool {
        return std::holds_alternative<S>(state_);
    }

private:
    state::State state_;
};

// ── Transition table ─────────────────────────────────────────────────────────

template <UiSinkConcept Sink>
auto StateMachine::process(event::Event evt, Sink& ui) -> Result<void>
{
    auto next = std::visit(Overloaded{

        // Idle / WaitingForUser + UserMessage → Thinking
        [&](state::Idle&, event::UserMessage) -> Result<state::State> {
            ui(UiStatus{"Thinking..."});
            return state::Thinking{};
        },
        [&](state::WaitingForUser&, event::UserMessage) -> Result<state::State> {
            ui(UiStatus{"Thinking..."});
            return state::Thinking{};
        },

        // Thinking + ApiResponseStarted → Streaming
        [&](state::Thinking&, event::ApiResponseStarted e) -> Result<state::State> {
            return state::Streaming{
                .message_id       = std::move(e.id),
                .accumulated_text = {},
                .tool_json_accum  = {},
                .pending_tool_ids = {}
            };
        },

        // Streaming + TextDelta → Streaming  (hot path)
        [&](state::Streaming& s, event::TextDelta e) -> Result<state::State> {
            s.accumulated_text += e.text;
            ui(UiTokens{std::move(e.text)});
            return std::move(s);
        },

        // Streaming + InputJsonDelta → Streaming (accumulate tool JSON)
        [&](state::Streaming& s, event::InputJsonDelta e) -> Result<state::State> {
            s.tool_json_accum += e.partial_json;
            return std::move(s);
        },

        // Streaming + ContentBlockStart → Streaming
        [&](state::Streaming& s, event::ContentBlockStart e) -> Result<state::State> {
            if (e.type == ContentType::ToolUse) {
                s.pending_tool_ids.push_back(e.tool_use_id);
                s.tool_json_accum.clear();
                ui(UiToolQueued{std::move(e.tool_name), std::move(e.tool_use_id)});
            }
            return std::move(s);
        },

        // Streaming + ContentBlockStop → Streaming
        [&](state::Streaming& s, event::ContentBlockStop) -> Result<state::State> {
            return std::move(s);
        },

        // Streaming + MessageComplete → Done | ToolExec
        [&](state::Streaming&, event::MessageComplete e) -> Result<state::State> {
            if (e.reason == StopReason::ToolUse) return state::ToolExec{};
            ui(UiDone{});
            return state::Done{e.reason};
        },

        // ToolExec + ToolResultReady → ToolExec | Thinking
        [&](state::ToolExec& s, event::ToolResultReady e) -> Result<state::State> {
            ui(UiToolEnd{e.id, e.output, e.is_error, 0});
            if (++s.completed >= s.calls.size()) {
                ui(UiStatus{"Thinking..."});
                return state::Thinking{};
            }
            return std::move(s);
        },

        // Quit from any state → Idle
        [&](auto&, event::Quit) -> Result<state::State> {
            return state::Idle{};
        },

        // Error from any state → Failed
        [&](auto&, event::ErrorOccurred e) -> Result<state::State> {
            ui(UiError{e.error});
            return state::Failed{std::move(e.error)};
        },

        // Catch-all: ignore unexpected transitions (SSE can be noisy)
        [&](auto& s, auto) -> Result<state::State> {
            return std::move(s);
        }

    }, state_, evt);

    if (!next) return std::unexpected(next.error());
    state_ = std::move(*next);
    return {};
}

} // namespace clawed
