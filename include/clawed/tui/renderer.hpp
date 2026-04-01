#pragma once

// ── Renderer ─────────────────────────────────────────────────────────────────
// The terminal output engine. Two modes:
//
//   1. Compiled render: takes any Element/Component, layouts via LayoutCtx,
//      diffs against previous frame, writes minimal ANSI updates.
//      Fully monomorphic — the render<E>() template instantiates a direct
//      call to E::layout() with no indirection.
//
//   2. Streaming: hot-path token output bypasses the element tree entirely.
//      Single write() syscall per token, no allocation, no diffing.

#include <clawed/tui/element.hpp>
#include <clawed/tui/style.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace clawed::tui {

class Renderer {
public:
    /// Render any Element or Component. Monomorphic — zero virtual dispatch.
    template <Element E>
    void render(const E& root, int width = 80) {
        auto new_lines = do_layout(root, width);
        apply_diff(new_lines);
        prev_lines_ = std::move(new_lines);
    }

    /// Stream a token directly (hot path — no element tree, no diffing).
    void stream_token(std::string_view text) {
        write_raw(text);
    }

    /// Stream styled text directly.
    void stream_styled(std::string_view text, const Style& s) {
        write_raw(styled(text, s));
    }

    /// Write a rendered line.
    void write_line(const RenderedLine& line) {
        write_raw(line.to_string());
        write_raw("\n");
    }

    /// Write raw bytes to stdout.
    void write_raw(std::string_view s) {
        std::cout.write(s.data(), static_cast<std::streamsize>(s.size()));
    }

    void flush() { std::cout.flush(); }
    void newline() { write_raw("\n"); flush(); }

    /// Clear previous rendered frame from terminal.
    void clear_frame() {
        for (size_t i = 0; i < prev_lines_.size(); ++i)
            write_raw("\033[A\033[2K");
        prev_lines_.clear();
    }

    void reset() { prev_lines_.clear(); }

private:
    std::vector<RenderedLine> prev_lines_;

    void apply_diff(const std::vector<RenderedLine>& new_lines) {
        // Clear previous frame
        for (size_t i = 0; i < prev_lines_.size(); ++i)
            write_raw("\033[A\033[2K");

        // Write new frame
        for (auto& line : new_lines) {
            write_raw(line.to_string());
            write_raw("\n");
        }
        flush();
    }
};

} // namespace clawed::tui
