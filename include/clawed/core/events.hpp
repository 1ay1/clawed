#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>

namespace clawed::event {

struct UserMessage {
    std::string text;
};

struct ApiResponseStarted {
    MessageId id;
};

struct ContentBlockStart {
    size_t      index;
    ContentType type;
    std::string tool_use_id;   // only for ToolUse blocks
    std::string tool_name;     // only for ToolUse blocks
};

struct TextDelta {
    std::string text;
};

struct InputJsonDelta {
    std::string partial_json;
};

struct ContentBlockStop {
    size_t index;
};

struct MessageComplete {
    StopReason reason;
};

struct ToolResultReady {
    ToolUseId   id;
    std::string output;
    bool        is_error = false;
};

struct ErrorOccurred {
    Error error;
};

struct Quit {};

// ── Event variant ───────────────────────────────────────────────────────────
using Event = std::variant<
    UserMessage, ApiResponseStarted,
    ContentBlockStart, TextDelta, InputJsonDelta, ContentBlockStop,
    MessageComplete, ToolResultReady, ErrorOccurred, Quit>;

} // namespace clawed::event
