#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <clawed/api/messages.hpp>
#include <clawed/api/sse_parser.hpp>
#include <chrono>
#include <string>

// Forward declare curl handle type to avoid including curl.h in the header.
using CURL = void;

namespace clawed {

class ApiClient {
public:
    struct Config {
        std::string api_key;
        std::string base_url    = "https://api.anthropic.com";
        std::string api_version = "2023-06-01";
        std::chrono::seconds timeout{300};
    };

    explicit ApiClient(Config config);
    ~ApiClient();

    ApiClient(const ApiClient&) = delete;
    ApiClient& operator=(const ApiClient&) = delete;
    ApiClient(ApiClient&&) noexcept;
    ApiClient& operator=(ApiClient&&) noexcept;

    // Stream a messages request. Blocks until stream completes or errors.
    // Events are pushed to sink as they arrive.
    auto stream_message(const api::CreateMessageRequest& request,
                        SseParser::EventSink& sink) -> Result<void>;

private:
    Config config_;
    CURL*  curl_ = nullptr;

    void init_curl();
    void cleanup_curl();

    // curl write callback — feeds chunks into the SSE parser.
    static auto write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
        -> size_t;
};

} // namespace clawed
