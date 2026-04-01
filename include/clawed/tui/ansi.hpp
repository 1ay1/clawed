#pragma once

// ── ANSI escape code constants ──────────────────────────────────────────────
// constexpr const char* — zero allocation, usable in hot paths.
// Single source of truth for all TUI modules.

namespace clawed::tui::ansi {

constexpr auto RST   = "\033[0m";
constexpr auto BOLD  = "\033[1m";
constexpr auto DIM   = "\033[2m";
constexpr auto ITAL  = "\033[3m";
constexpr auto ULINE = "\033[4m";

constexpr auto R     = "\033[91m";  // red (bright)
constexpr auto G     = "\033[32m";  // green
constexpr auto Y     = "\033[33m";  // yellow
constexpr auto B     = "\033[34m";  // blue
constexpr auto C     = "\033[96m";  // cyan (bright)
constexpr auto W     = "\033[97m";  // white (bright)
constexpr auto GR    = "\033[90m";  // gray (dark)
constexpr auto GL    = "\033[37m";  // gray (light)

} // namespace clawed::tui::ansi
