#pragma once

// ── Components ───────────────────────────────────────────────────────────────
//
// Components are types satisfying the Component concept:
//
//   template <typename T>
//   concept Component = requires(const T& t) {
//       { t.render() } -> Element;
//   };
//
// Each component's render() returns a CONCRETE element type — no type erasure.
// The compiler sees the full tree structure and can inline everything.
//
// For components that need dynamic children (e.g. variable tool cards),
// render() returns a DynList or AnyElement — paying for dynamism only there.
//
// Component tree:
//   Banner         → VBox<Text, Text, Empty>                    (static)
//   ToolCardView   → DynList (dynamic children)                 (dynamic)
//   StatusLine     → Text | Empty                               (static via variant)
//   ErrorDisplay   → Text | Empty                               (static via variant)

#include <clawed/tui/element.hpp>
#include <clawed/tui/style.hpp>

#include <concepts>
#include <string>
#include <vector>

namespace clawed::tui {

// ── Component concept ────────────────────────────────────────────────────────

template <typename T>
concept Component = requires(const T& t, LayoutCtx& ctx) {
    // A component can layout itself. This is the same as Element —
    // components ARE elements. Composition, not inheritance.
    { t.layout(ctx) } -> std::same_as<void>;
};

// static_assert that Component subsumes Element
static_assert(Component<Text>);
static_assert(Component<Empty>);
static_assert(Component<Divider>);
static_assert(Component<DynList>);

namespace components {

// ── ToolCard ─────────────────────────────────────────────────────────────────

struct ToolCardProps {
    std::string name;
    std::string id;
    std::string result;
    bool        is_error  = false;
    bool        completed = false;
};

/// Renders a tool invocation card. Returns a DynList because the number
/// of children depends on completion state (runtime).
struct ToolCardView {
    ToolCardProps props;

    void layout(LayoutCtx& ctx) const {
        auto short_id = props.id.size() > 8 ? props.id.substr(0, 8) : props.id;

        // Header
        hbox(
            text("-- ", styles::gray),
            text(props.name, styles::cyan),
            text(" [", styles::gray),
            text(short_id, styles::gray),
            text("] ", styles::gray),
            text(std::string(20, '-'), styles::gray)
        ).layout(ctx);

        if (props.completed) {
            auto result_text = props.result;
            if (result_text.size() > 500)
                result_text = result_text.substr(0, 500) + "... [truncated]";

            auto result_style = props.is_error ? styles::error : styles::muted;
            if (props.is_error)
                text("[error] " + result_text, result_style).layout(ctx);
            else
                text(result_text, result_style).layout(ctx);

            // Footer
            hbox(
                text("-- end ", styles::gray),
                text(short_id, styles::gray),
                text(" ", styles::gray),
                text(std::string(20, '-'), styles::gray)
            ).layout(ctx);
        } else {
            text("running...", styles::muted).layout(ctx);
        }
    }
};

// Verify it satisfies Component
static_assert(Component<ToolCardView>);

// ── StatusLine ───────────────────────────────────────────────────────────────

struct StatusLine {
    std::string message;

    void layout(LayoutCtx& ctx) const {
        if (!message.empty())
            text(message, styles::muted).layout(ctx);
    }
};

static_assert(Component<StatusLine>);

// ── ErrorDisplay ─────────────────────────────────────────────────────────────

struct ErrorDisplay {
    std::string message;

    void layout(LayoutCtx& ctx) const {
        if (!message.empty())
            text(message, styles::error).layout(ctx);
    }
};

static_assert(Component<ErrorDisplay>);

// ── Banner ───────────────────────────────────────────────────────────────────

struct Banner {
    void layout(LayoutCtx& ctx) const {
        vbox(
            text("clawed v0.1.0 -- Claude agent runtime", styles::heading),
            text("Type your message, or 'quit' to exit.", styles::muted),
            empty()
        ).layout(ctx);
    }
};

static_assert(Component<Banner>);

// ── TurnView ─────────────────────────────────────────────────────────────────
// Full turn output. Uses DynList for the tool cards (runtime-variable count).

struct TurnState {
    std::string              streamed_text;
    std::vector<ToolCardProps> tool_cards;
    std::string              status;
    std::string              error;
    bool                     done = false;
};

struct TurnView {
    const TurnState& state;

    void layout(LayoutCtx& ctx) const {
        // Status (only when no text yet)
        if (!state.status.empty() && state.streamed_text.empty())
            StatusLine{state.status}.layout(ctx);

        // Streamed text
        if (!state.streamed_text.empty())
            text(state.streamed_text).layout(ctx);

        // Tool cards (dynamic)
        for (auto& card : state.tool_cards)
            ToolCardView{card}.layout(ctx);

        // Error
        if (!state.error.empty())
            ErrorDisplay{state.error}.layout(ctx);
    }
};

static_assert(Component<TurnView>);

} // namespace components
} // namespace clawed::tui
