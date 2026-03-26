#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <clawed/core/events.hpp>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace clawed {

// ── Incremental SSE stream parser ───────────────────────────────────────────
// Fed raw bytes from curl. Produces typed events as they complete.
// Handles partial lines across chunk boundaries.

class SseParser {
public:
    using EventSink = std::function<void(event::Event)>;

    // Feed a chunk of raw bytes. May emit 0..N events via sink.
    void feed(std::span<const char> chunk, EventSink& sink);

    // Reset parser state for a new stream.
    void reset();

private:
    std::string line_buffer_;       // partial line accumulator
    std::string event_type_;        // current "event:" value
    std::string data_buffer_;       // current "data:" value(s)

    // Process a complete SSE line (without trailing \n).
    void process_line(std::string_view line, EventSink& sink);

    // Dispatch a complete SSE event (type + data).
    void dispatch_event(EventSink& sink);

    // Parse the JSON payload of a specific SSE event type into our Event.
    auto parse_payload(std::string_view type, std::string_view data)
        -> std::optional<event::Event>;
};

} // namespace clawed
