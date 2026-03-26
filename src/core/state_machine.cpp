#include <clawed/core/state_machine.hpp>
#include <format>

namespace clawed {

auto StateMachine::process(event::Event evt, UiSink& ui) -> Result<void> {
    auto result = std::visit(Overloaded{

        // ── Idle / WaitingForUser + UserMessage → Thinking ──────────────
        [&](state::Idle&, event::UserMessage) -> Result<state::State> {
            ui(UiStatus{"Thinking..."});
            return state::Thinking{};
        },
        [&](state::WaitingForUser&, event::UserMessage) -> Result<state::State> {
            ui(UiStatus{"Thinking..."});
            return state::Thinking{};
        },

        // ── Thinking + ApiResponseStarted → Streaming ───────────────────
        [&](state::Thinking&, event::ApiResponseStarted e) -> Result<state::State> {
            return state::Streaming{
                .message_id       = std::move(e.id),
                .accumulated_text = {},
                .tool_json_accum  = {},
                .pending_tool_ids = {}
            };
        },

        // ── Streaming + TextDelta → Streaming (self-loop, hot path) ─────
        [&](state::Streaming& s, event::TextDelta e) -> Result<state::State> {
            s.accumulated_text += e.text;
            ui(UiTokens{std::move(e.text)});
            return std::move(s);
        },

        // ── Streaming + InputJsonDelta → Streaming (accumulate tool JSON)
        [&](state::Streaming& s, event::InputJsonDelta e) -> Result<state::State> {
            s.tool_json_accum += e.partial_json;
            return std::move(s);
        },

        // ── Streaming + ContentBlockStart → Streaming ───────────────────
        [&](state::Streaming& s, event::ContentBlockStart e) -> Result<state::State> {
            if (e.type == ContentType::ToolUse) {
                s.pending_tool_ids.push_back(e.tool_use_id);
                s.tool_json_accum.clear();
                ui(UiToolStart{std::move(e.tool_name), std::move(e.tool_use_id)});
            }
            return std::move(s);
        },

        // ── Streaming + ContentBlockStop → Streaming ────────────────────
        [&](state::Streaming& s, event::ContentBlockStop) -> Result<state::State> {
            return std::move(s);
        },

        // ── Streaming + MessageComplete → Done or ToolExec ──────────────
        [&](state::Streaming&, event::MessageComplete e) -> Result<state::State> {
            if (e.reason == StopReason::ToolUse) {
                return state::ToolExec{};
            }
            ui(UiDone{});
            return state::Done{e.reason};
        },

        // ── ToolExec + ToolResultReady → ToolExec or Thinking ───────────
        [&](state::ToolExec& s, event::ToolResultReady e) -> Result<state::State> {
            ui(UiToolEnd{e.id, e.output, e.is_error});
            ++s.completed;
            if (s.completed >= s.calls.size()) {
                ui(UiStatus{"Thinking..."});
                return state::Thinking{};
            }
            return std::move(s);
        },

        // ── Quit from any state → Idle ──────────────────────────────────
        [&](auto&, event::Quit) -> Result<state::State> {
            return state::Idle{};
        },

        // ── Error from any state → Failed ───────────────────────────────
        [&](auto&, event::ErrorOccurred e) -> Result<state::State> {
            ui(UiError{e.error});
            return state::Failed{std::move(e.error)};
        },

        // ── Catch-all: invalid transition (no-op, stay in current state) ─
        [&](auto& s, auto) -> Result<state::State> {
            // For robustness during streaming, ignore unexpected events
            // rather than erroring. The SSE stream can produce events
            // in any order during edge cases.
            return std::move(s);
        }

    }, state_, evt);

    if (!result) {
        return std::unexpected(result.error());
    }

    state_ = std::move(*result);
    return {};
}

} // namespace clawed
