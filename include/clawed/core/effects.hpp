#pragma once

#include <clawed/core/error.hpp>
#include <string>
#include <variant>
#include <vector>

namespace clawed::effect {

// ── Effect types ─────────────────────────────────────────────────────────────
// Each is a plain data struct. The state machine produces a list of these
// as output of every transition. Callers decide what to do with them.

struct Token    { std::string text; };                             // stream text chunk
struct Status   { std::string msg;  };                             // status/spinner line
struct ToolCall { std::string name; std::string id; };             // tool invocation start
struct ToolDone { std::string id; std::string result; bool err; }; // tool result
struct Done     {};                                                // turn complete
struct Fail     { Error error; };                                  // unrecoverable error

using Effect  = std::variant<Token, Status, ToolCall, ToolDone, Done, Fail>;
using Effects = std::vector<Effect>;

} // namespace clawed::effect
