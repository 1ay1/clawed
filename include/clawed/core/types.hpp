#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <variant>

namespace clawed {

// ── Forward declarations ────────────────────────────────────────────────────
struct Error;

template <typename T>
using Result = std::expected<T, Error>;

using StrView = std::string_view;
using MessageId = std::string;
using ToolUseId = std::string;

// ── Enums ───────────────────────────────────────────────────────────────────
enum class ContentType : uint8_t { Text, Thinking, ToolUse, ToolResult };
enum class StopReason : uint8_t { EndTurn, ToolUse, MaxTokens, StopSequence };
enum class Role : uint8_t { User, Assistant };

// ── Token callback (hot path) ───────────────────────────────────────────────
using TokenCallback = std::function<void(StrView token)>;

// ── Overloaded visitor helper ───────────────────────────────────────────────
template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

} // namespace clawed
