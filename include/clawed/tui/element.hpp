#pragma once

// ── Element system (compile-time virtual DOM) ────────────────────────────────
//
// The element tree is encoded in the TYPE SYSTEM. Layout composition is
// resolved at compile time via fold expressions and template recursion.
// No heap allocation, no variant dispatch, no virtual calls for static trees.
//
// Static elements (compile-time known structure):
//   auto el = vbox(
//       text("Hello", styles::bold),
//       hbox(
//           text("[", styles::gray),
//           text("tool_use", styles::cyan),
//           text("]", styles::gray)
//       ),
//       divider()
//   );
//   // Type: VBox<Text, HBox<Text, Text, Text>, Divider>
//   // Zero heap allocation. Layout fully inlined by optimizer.
//
// Dynamic elements (runtime-variable children):
//   DynList list;
//   for (auto& card : cards) list.push(ToolCard(card));
//   // Uses type-erased AnyElement internally — pay for dynamism only here.
//
// Concept:
//   template <typename T>
//   concept Element = requires(const T& t, LayoutCtx& ctx) {
//       { t.layout(ctx) };
//   };

#include <clawed/tui/style.hpp>

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace clawed::tui {

// Forward declaration
class LayoutCtx;

// ── Element concept ──────────────────────────────────────────────────────────

template <typename T>
concept Element = requires(const T& t, LayoutCtx& ctx) {
    { t.layout(ctx) } -> std::same_as<void>;
};

// ── Span ─────────────────────────────────────────────────────────────────────
// The atomic unit of styled text. Used for both elements and rendered output.

struct Span {
    std::string text;
    Style       style;

    Span() = default;
    Span(std::string t, Style s = {}) : text(std::move(t)), style(s) {}
    Span(const char* t, Style s = {}) : text(t), style(s) {}
};

// ── Primitive elements ───────────────────────────────────────────────────────

/// Single styled text. Satisfies Element.
struct Text {
    std::string content;
    Style       style;

    void layout(LayoutCtx& ctx) const;
};

/// Horizontal rule. Satisfies Element.
struct Divider {
    Style style = styles::gray;

    void layout(LayoutCtx& ctx) const;
};

/// Empty element (renders nothing). Satisfies Element.
struct Empty {
    void layout(LayoutCtx&) const {}
};

// ── Compound elements (compile-time) ─────────────────────────────────────────

/// Vertical stack. Children are a tuple — fully monomorphic, zero allocation.
template <Element... Es>
struct VBox {
    std::tuple<Es...> children;

    explicit VBox(Es... args) : children(std::move(args)...) {}

    void layout(LayoutCtx& ctx) const {
        std::apply([&](const auto&... child) {
            (child.layout(ctx), ...);
        }, children);
    }
};

/// Horizontal concatenation (single line). Children laid out inline.
template <Element... Es>
struct HBox {
    std::tuple<Es...> children;

    explicit HBox(Es... args) : children(std::move(args)...) {}

    void layout(LayoutCtx& ctx) const;
};

/// Bordered block with title. Content is a single Element (compose with VBox).
template <Element E>
struct Block {
    std::string title;
    Style       border_style;
    E           content;

    void layout(LayoutCtx& ctx) const;
};

// ── Type-erased element (for dynamic children) ──────────────────────────────

class AnyElement {
public:
    template <Element E>
    AnyElement(E el)
        : impl_(std::make_unique<Model<E>>(std::move(el))) {}

    AnyElement(AnyElement&&) noexcept = default;
    AnyElement& operator=(AnyElement&&) noexcept = default;

    AnyElement(const AnyElement& other)
        : impl_(other.impl_ ? other.impl_->clone() : nullptr) {}
    AnyElement& operator=(const AnyElement& other) {
        impl_ = other.impl_ ? other.impl_->clone() : nullptr;
        return *this;
    }

    void layout(LayoutCtx& ctx) const { if (impl_) impl_->layout(ctx); }

private:
    struct Concept {
        virtual ~Concept() = default;
        virtual void layout(LayoutCtx& ctx) const = 0;
        virtual auto clone() const -> std::unique_ptr<Concept> = 0;
    };

    template <Element E>
    struct Model final : Concept {
        E element;
        explicit Model(E e) : element(std::move(e)) {}
        void layout(LayoutCtx& ctx) const override { element.layout(ctx); }
        auto clone() const -> std::unique_ptr<Concept> override {
            return std::make_unique<Model>(*this);
        }
    };

    std::unique_ptr<Concept> impl_;
};

/// Dynamic list of elements. Use when the number of children is runtime-variable.
struct DynList {
    std::vector<AnyElement> children;

    template <Element E>
    void push(E el) { children.emplace_back(std::move(el)); }

    void layout(LayoutCtx& ctx) const {
        for (auto& child : children) child.layout(ctx);
    }
};

// ── Layout context ───────────────────────────────────────────────────────────
// Accumulates rendered lines. Passed through the element tree.

struct RenderedLine {
    std::vector<Span> spans;

    [[nodiscard]] auto to_string() const -> std::string {
        std::string out;
        for (auto& s : spans) out += styled(s.text, s.style);
        return out;
    }

    bool operator==(const RenderedLine& o) const {
        if (spans.size() != o.spans.size()) return false;
        for (size_t i = 0; i < spans.size(); ++i)
            if (spans[i].text != o.spans[i].text || !(spans[i].style == o.spans[i].style))
                return false;
        return true;
    }
};

class LayoutCtx {
public:
    explicit LayoutCtx(int width = 80) : width_(width) {}

    /// Push a complete line.
    void push_line(std::vector<Span> spans) {
        lines_.push_back(RenderedLine{std::move(spans)});
    }

    /// Push a single-span line.
    void push_line(Span s) {
        lines_.push_back(RenderedLine{{std::move(s)}});
    }

    /// Begin accumulating spans into a single line (for HBox).
    void begin_inline() { inline_spans_.clear(); inline_mode_ = true; }

    /// Add a span to the current inline accumulator.
    void push_inline(Span s) { inline_spans_.push_back(std::move(s)); }

    /// Finish inline mode, flush accumulated spans as one line.
    void end_inline() {
        if (!inline_spans_.empty())
            lines_.push_back(RenderedLine{std::move(inline_spans_)});
        inline_mode_ = false;
    }

    [[nodiscard]] bool in_inline() const { return inline_mode_; }
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] auto lines() && -> std::vector<RenderedLine> { return std::move(lines_); }
    [[nodiscard]] auto lines() const & -> const std::vector<RenderedLine>& { return lines_; }

private:
    int width_;
    std::vector<RenderedLine> lines_;
    std::vector<Span> inline_spans_;
    bool inline_mode_ = false;
};

// ── Primitive layout implementations ─────────────────────────────────────────

inline void Text::layout(LayoutCtx& ctx) const {
    if (ctx.in_inline()) {
        ctx.push_inline(Span{content, style});
    } else {
        ctx.push_line(Span{content, style});
    }
}

inline void Divider::layout(LayoutCtx& ctx) const {
    ctx.push_line(Span{std::string(std::min(ctx.width(), 60), '-'), style});
}

template <Element... Es>
void HBox<Es...>::layout(LayoutCtx& ctx) const {
    ctx.begin_inline();
    std::apply([&](const auto&... child) {
        (child.layout(ctx), ...);
    }, children);
    ctx.end_inline();
}

template <Element E>
void Block<E>::layout(LayoutCtx& ctx) const {
    // Top border
    std::string top = "--- ";
    if (!title.empty()) top += title + " ";
    while (static_cast<int>(top.size()) < ctx.width() - 1) top += "-";
    ctx.push_line(Span{top, border_style});

    // Content (indented via sub-context)
    LayoutCtx sub(ctx.width() - 4);
    content.layout(sub);
    for (auto& line : std::move(sub).lines()) {
        std::vector<Span> indented;
        indented.emplace_back("  ", border_style);
        for (auto& s : line.spans) indented.push_back(std::move(s));
        ctx.push_line(std::move(indented));
    }

    // Bottom border
    ctx.push_line(Span{std::string(std::min(ctx.width(), 40), '-'), border_style});
}

// ── Builder functions ────────────────────────────────────────────────────────
// Return concrete types — no type erasure, no allocation.

inline auto text(std::string content, Style style = {}) -> Text {
    return Text{std::move(content), style};
}

inline auto text(const char* content, Style style = {}) -> Text {
    return Text{content, style};
}

inline auto divider(Style style = styles::gray) -> Divider {
    return Divider{style};
}

inline constexpr auto empty() -> Empty { return {}; }

template <Element... Es>
auto vbox(Es... children) -> VBox<Es...> {
    return VBox<Es...>(std::move(children)...);
}

template <Element... Es>
auto hbox(Es... children) -> HBox<Es...> {
    return HBox<Es...>(std::move(children)...);
}

template <Element E>
auto block(std::string title, E content, Style border_style = styles::gray)
    -> Block<E>
{
    return Block<E>{std::move(title), border_style, std::move(content)};
}

// ── Layout helper ────────────────────────────────────────────────────────────

template <Element E>
auto do_layout(const E& el, int width = 80) -> std::vector<RenderedLine> {
    LayoutCtx ctx(width);
    el.layout(ctx);
    return std::move(ctx).lines();
}

} // namespace clawed::tui
