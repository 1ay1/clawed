#pragma once

#include <clawed/api/messages.hpp>
#include <clawed/api/sse_parser.hpp>
#include <clawed/auth/auth.hpp>
#include <clawed/core/error.hpp>
#include <chrono>
#include <format>
#include <span>
#include <string>
#include <curl/curl.h>

namespace clawed {

class ApiClient {
public:
    struct Config {
        auth::Credentials    credentials;
        std::string          base_url    = "https://api.anthropic.com";
        std::string          api_version = "2023-06-01";
        std::chrono::seconds timeout{300};
    };

    explicit ApiClient(Config config);
    ~ApiClient();

    ApiClient(const ApiClient&)            = delete;
    ApiClient& operator=(const ApiClient&) = delete;
    ApiClient(ApiClient&&) noexcept;
    ApiClient& operator=(ApiClient&&) noexcept;

    // Stream a messages request. Blocks until stream completes or errors.
    // Sink is resolved at compile time — zero virtual dispatch on the hot path.
    template <EventSinkConcept Sink>
    auto stream_message(const api::CreateMessageRequest& request, Sink& sink)
        -> Result<void>;

private:
    Config config_;
    CURL*  curl_ = nullptr;

    void init_curl();
    void cleanup_curl();
    void ensure_fresh_token();

    // Per-request context — templated so write_cb can call sink directly.
    template <EventSinkConcept Sink>
    struct StreamCtx {
        SseParser   parser;
        Sink*       sink_ptr  = nullptr;
        bool        had_error = false;
        std::string error_msg;
        std::string raw_body;
    };

    template <EventSinkConcept Sink>
    static auto write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
        -> size_t
    {
        auto  total = size * nmemb;
        auto* ctx   = static_cast<StreamCtx<Sink>*>(userdata);
        try {
            ctx->raw_body.append(ptr, total);
            ctx->parser.feed(std::span<const char>(ptr, total), *ctx->sink_ptr);
        } catch (const std::exception& ex) {
            ctx->had_error = true;
            ctx->error_msg = ex.what();
            return 0;
        }
        return total;
    }
};

// ── stream_message implementation ────────────────────────────────────────────

template <EventSinkConcept Sink>
auto ApiClient::stream_message(const api::CreateMessageRequest& request, Sink& sink)
    -> Result<void>
{
    if (!curl_) {
        return make_error(ErrorCode::HttpConnectionFailed, "curl not initialized");
    }

    ensure_fresh_token();

    const auto url  = std::format("{}/v1/messages", config_.base_url);
    const auto body = request.to_json().dump();
    const auto auth_header = std::format("{}: {}",
        config_.credentials.header_name(),
        config_.credentials.header_value());

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers,
        std::format("anthropic-version: {}", config_.api_version).c_str());
    if (config_.credentials.method == auth::AuthMethod::OAuthToken) {
        headers = curl_slist_append(headers, "anthropic-beta: oauth-2025-04-20");
    }
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    StreamCtx<Sink> ctx;
    ctx.parser.reset();
    ctx.sink_ptr = &sink;

    curl_easy_setopt(curl_, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST,           1L);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS,     body.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE,  static_cast<long>(body.size()));
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION,  write_cb<Sink>);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA,      &ctx);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT,
        static_cast<long>(config_.timeout.count()));
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);

    const CURLcode res = curl_easy_perform(curl_);

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_reset(curl_);

    if (res != CURLE_OK) {
        if (ctx.had_error)
            return make_error(ErrorCode::SseParseError, ctx.error_msg);
        if (res == CURLE_OPERATION_TIMEDOUT)
            return make_error(ErrorCode::HttpTimeout, "request timed out");
        return make_error(ErrorCode::HttpConnectionFailed,
            std::format("curl error: {}", curl_easy_strerror(res)));
    }

    if (http_code == 401) {
        return make_error(ErrorCode::ApiAuthError,
            "authentication failed (401). Run 'clawed login' to re-authenticate.");
    }

    if (http_code >= 400) {
        return make_error(ErrorCode::ApiError,
            std::format("API error (HTTP {}): {}",
                http_code, ctx.raw_body.substr(0, 500)));
    }

    return {};
}

} // namespace clawed
