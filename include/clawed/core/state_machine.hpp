#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <clawed/core/states.hpp>
#include <clawed/core/events.hpp>
#include <functional>
#include <variant>

namespace clawed {

// ── UI notification sink ────────────────────────────────────────────────────

struct UiTokens     { std::string text; };
struct UiToolStart  { std::string name; std::string id; };
struct UiToolEnd    { std::string id; std::string result; bool is_error; };
struct UiStatus     { std::string message; };
struct UiError      { Error error; };
struct UiDone       {};

using UiEvent = std::variant<UiTokens, UiToolStart, UiToolEnd,
                             UiStatus, UiError, UiDone>;
using UiSink  = std::function<void(UiEvent)>;

// ── State Machine ───────────────────────────────────────────────────────────
// Compile-time dispatched via std::visit over (State, Event).

class StateMachine {
public:
    StateMachine() : state_(state::Idle{}) {}

    auto process(event::Event evt, UiSink& ui) -> Result<void>;

    [[nodiscard]] auto current() const -> const state::State& { return state_; }

    template <typename S>
    [[nodiscard]] auto is() const -> bool {
        return std::holds_alternative<S>(state_);
    }

private:
    state::State state_;
};

} // namespace clawed
