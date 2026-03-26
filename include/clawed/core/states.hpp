#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <string>
#include <variant>
#include <vector>

namespace clawed::state {

// ── Each state is a distinct type carrying only its relevant data ────────────

struct Idle {};

struct WaitingForUser {};

struct Thinking {
    MessageId request_id;
};

struct Streaming {
    MessageId            message_id;
    std::string          accumulated_text;
    std::string          tool_json_accum; // accumulates partial tool input JSON
    std::vector<ToolUseId> pending_tool_ids;
};

struct ToolExec {
    struct PendingCall {
        ToolUseId   id;
        std::string name;
        std::string input_json;
    };
    std::vector<PendingCall> calls;
    size_t                   completed = 0;
};

struct Done {
    StopReason reason;
};

struct Failed {
    Error error;
};

// ── The central state variant ───────────────────────────────────────────────
using State = std::variant<Idle, WaitingForUser, Thinking, Streaming,
                           ToolExec, Done, Failed>;

} // namespace clawed::state
