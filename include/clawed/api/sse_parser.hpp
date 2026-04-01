#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <clawed/core/events.hpp>
#include <concepts>
#include <optional>
#include <span>
#include <string>

namespace clawed {

// ── Concept: any callable that accepts a streaming event ────────────────────

template <typename F>
concept EventSinkConcept = std::invocable<F, event::Event>;

// ── Incremental SSE stream parser ────────────────────────────────────────────
// Fed raw bytes from curl. Produces typed events as they complete.
// feed() is a template — the sink type is resolved at compile time,
// giving the optimizer a straight-line path from raw bytes to event handling.

class SseParser {
public:
    template <EventSinkConcept Sink>
    void feed(std::span<const char> chunk, Sink& sink) {
        for (char c : chunk) {
            if (c == '\n') {
                process_line(line_buffer_, sink);
                line_buffer_.clear();
            } else if (c != '\r') {
                line_buffer_ += c;
            }
        }
    }

    void reset();

private:
    std::string line_buffer_;
    std::string event_type_;
    std::string data_buffer_;

    template <EventSinkConcept Sink>
    void process_line(std::string_view line, Sink& sink) {
        if (line.empty()) {
            if (!data_buffer_.empty()) dispatch_event(sink);
            event_type_.clear();
            data_buffer_.clear();
            return;
        }
        if (line.starts_with("event:")) {
            event_type_ = line.substr(6);
            if (!event_type_.empty() && event_type_[0] == ' ')
                event_type_ = event_type_.substr(1);
        } else if (line.starts_with("data:")) {
            auto data = line.substr(5);
            if (!data.empty() && data[0] == ' ') data = data.substr(1);
            if (!data_buffer_.empty()) data_buffer_ += '\n';
            data_buffer_ += data;
        }
        // Ignore id:, retry:, comments — not needed for Anthropic SSE.
    }

    template <EventSinkConcept Sink>
    void dispatch_event(Sink& sink) {
        auto evt = parse_payload(event_type_, data_buffer_);
        if (evt) sink(std::move(*evt));
    }

    // JSON parsing lives in sse_parser.cpp — keeps the header light.
    auto parse_payload(std::string_view type, std::string_view data)
        -> std::optional<event::Event>;
};

} // namespace clawed
