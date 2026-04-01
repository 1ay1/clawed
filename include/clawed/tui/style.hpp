#pragma once

// ── Terminal styling ─────────────────────────────────────────────────────────
// Compile-time composable styles. Renders to ANSI escape sequences.
// Designed for zero-cost: Style is a trivial value type (fits in a register).

#include <cstdint>
#include <string>

namespace clawed::tui {

enum class Color : uint8_t {
    Default      = 0,
    Black        = 30,
    Red          = 31,
    Green        = 32,
    Yellow       = 33,
    Blue         = 34,
    Magenta      = 35,
    Cyan         = 36,
    White        = 37,
    BrightBlack  = 90,   // Gray
    BrightRed    = 91,
    BrightGreen  = 92,
    BrightYellow = 93,
    BrightBlue   = 94,
    BrightMagenta= 95,
    BrightCyan   = 96,
    BrightWhite  = 97,
};

struct Style {
    Color fg        = Color::Default;
    Color bg        = Color::Default;
    bool  bold      = false;
    bool  dim       = false;
    bool  italic    = false;
    bool  underline = false;

    // Fluent builder — each returns a new Style (value type, cheap copy).
    [[nodiscard]] constexpr auto with_fg(Color c) const -> Style {
        auto s = *this; s.fg = c; return s;
    }
    [[nodiscard]] constexpr auto with_bg(Color c) const -> Style {
        auto s = *this; s.bg = c; return s;
    }
    [[nodiscard]] constexpr auto with_bold() const -> Style {
        auto s = *this; s.bold = true; return s;
    }
    [[nodiscard]] constexpr auto with_dim() const -> Style {
        auto s = *this; s.dim = true; return s;
    }
    [[nodiscard]] constexpr auto with_italic() const -> Style {
        auto s = *this; s.italic = true; return s;
    }
    [[nodiscard]] constexpr auto with_underline() const -> Style {
        auto s = *this; s.underline = true; return s;
    }

    constexpr bool operator==(const Style&) const = default;

    /// Returns true if this is the default (no styling needed).
    [[nodiscard]] constexpr bool is_default() const {
        return fg == Color::Default && bg == Color::Default
            && !bold && !dim && !italic && !underline;
    }
};

// ── Predefined styles ────────────────────────────────────────────────────────

namespace styles {
    inline constexpr Style plain{};
    inline constexpr Style bold   = Style{}.with_bold();
    inline constexpr Style dim    = Style{}.with_dim();
    inline constexpr Style italic = Style{}.with_italic();

    inline constexpr Style red     = Style{}.with_fg(Color::Red);
    inline constexpr Style green   = Style{}.with_fg(Color::Green);
    inline constexpr Style yellow  = Style{}.with_fg(Color::Yellow);
    inline constexpr Style blue    = Style{}.with_fg(Color::Blue);
    inline constexpr Style cyan    = Style{}.with_fg(Color::Cyan);
    inline constexpr Style magenta = Style{}.with_fg(Color::Magenta);
    inline constexpr Style gray    = Style{}.with_fg(Color::BrightBlack);

    inline constexpr Style error   = Style{}.with_fg(Color::Red).with_bold();
    inline constexpr Style success = Style{}.with_fg(Color::Green).with_bold();
    inline constexpr Style prompt  = Style{}.with_fg(Color::Green).with_bold();
    inline constexpr Style heading = Style{}.with_fg(Color::Cyan).with_bold();
    inline constexpr Style muted   = Style{}.with_fg(Color::BrightBlack);
}

// ── ANSI rendering ───────────────────────────────────────────────────────────

inline auto style_begin(const Style& s) -> std::string {
    if (s.is_default()) return {};
    std::string esc = "\033[";
    bool first = true;
    auto add = [&](int code) {
        if (!first) esc += ';';
        esc += std::to_string(code);
        first = false;
    };
    if (s.bold)      add(1);
    if (s.dim)       add(2);
    if (s.italic)    add(3);
    if (s.underline) add(4);
    if (s.fg != Color::Default) add(static_cast<int>(s.fg));
    if (s.bg != Color::Default) add(static_cast<int>(s.bg) + 10);
    esc += 'm';
    return esc;
}

inline constexpr auto style_end() -> std::string_view {
    return "\033[0m";
}

/// Wrap text in style escape codes.
inline auto styled(std::string_view text, const Style& s) -> std::string {
    if (s.is_default()) return std::string(text);
    std::string out;
    out.reserve(text.size() + 16);
    out += style_begin(s);
    out += text;
    out += style_end();
    return out;
}

} // namespace clawed::tui
