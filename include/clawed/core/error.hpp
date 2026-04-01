#pragma once

#include <cstdint>
#include <source_location>
#include <string>
#include <format>

namespace clawed {

enum class ErrorCode : uint16_t {
    // Network 1xx
    HttpConnectionFailed = 100,
    HttpTimeout          = 101,
    SseParseError        = 102,

    // API 2xx
    ApiAuthError        = 200,
    ApiRateLimit        = 201,
    ApiOverloaded       = 202,
    ApiInvalidRequest   = 203,
    ApiError            = 204,

    // Tool 3xx
    ToolNotFound         = 300,
    ToolExecFailed       = 301,
    ToolTimeout          = 302,
    SshConnectionFailed  = 303,

    // Internal 4xx
    InvalidState   = 400,
    JsonParseError = 401,
    ConfigError    = 402,
};

struct Error {
    ErrorCode   code;
    std::string message;
    std::source_location location;

    Error(ErrorCode c, std::string msg,
          std::source_location loc = std::source_location::current())
        : code(c), message(std::move(msg)), location(loc) {}

    [[nodiscard]] auto formatted() const -> std::string {
        return std::format("[E{}] {} ({}:{})",
            static_cast<uint16_t>(code), message,
            location.file_name(), location.line());
    }
};

// Convenience factory
inline auto make_error(ErrorCode code, std::string msg,
                       std::source_location loc = std::source_location::current())
    -> std::unexpected<Error>
{
    return std::unexpected<Error>(Error{code, std::move(msg), loc});
}

} // namespace clawed
