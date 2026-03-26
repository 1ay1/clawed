#include <clawed/api/client.hpp>
#include <curl/curl.h>
#include <format>
#include <iostream>

namespace clawed {

// ── Callback context passed through curl userdata ───────────────────────────
struct StreamContext {
    SseParser            parser;
    SseParser::EventSink sink;
    bool                 had_error = false;
    std::string          error_msg;
    std::string          raw_body;  // for error diagnostics
};

// ── Construction / destruction ──────────────────────────────────────────────

ApiClient::ApiClient(Config config) : config_(std::move(config)) {
    init_curl();
}

ApiClient::~ApiClient() {
    cleanup_curl();
}

ApiClient::ApiClient(ApiClient&& other) noexcept
    : config_(std::move(other.config_)), curl_(other.curl_) {
    other.curl_ = nullptr;
}

ApiClient& ApiClient::operator=(ApiClient&& other) noexcept {
    if (this != &other) {
        cleanup_curl();
        config_     = std::move(other.config_);
        curl_       = other.curl_;
        other.curl_ = nullptr;
    }
    return *this;
}

void ApiClient::init_curl() {
    curl_ = curl_easy_init();
}

void ApiClient::cleanup_curl() {
    if (curl_) {
        curl_easy_cleanup(static_cast<::CURL*>(curl_));
        curl_ = nullptr;
    }
}

// ── Write callback ──────────────────────────────────────────────────────────

auto ApiClient::write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
    -> size_t
{
    auto total = size * nmemb;
    auto* ctx  = static_cast<StreamContext*>(userdata);

    try {
        ctx->raw_body.append(ptr, total);
        ctx->parser.feed(std::span<const char>(ptr, total), ctx->sink);
    } catch (const std::exception& ex) {
        ctx->had_error = true;
        ctx->error_msg = ex.what();
        return 0; // signal curl to abort
    }

    return total;
}

// ── Token refresh ───────────────────────────────────────────────────────────

void ApiClient::ensure_fresh_token() {
    auto& creds = config_.credentials;
    if (creds.method != auth::AuthMethod::OAuthToken) return;
    if (!creds.is_expired()) return;
    if (creds.refresh_token.empty()) return;

    auto refreshed = auth::refresh_access_token(creds.refresh_token);
    if (refreshed) {
        config_.credentials = std::move(*refreshed);
        auth::save_credentials(config_.credentials);
    }
}

// ── Streaming request ───────────────────────────────────────────────────────

auto ApiClient::stream_message(const api::CreateMessageRequest& request,
                               SseParser::EventSink& sink) -> Result<void>
{
    if (!curl_) {
        return make_error(ErrorCode::HttpConnectionFailed, "curl not initialized");
    }

    // Refresh OAuth token if expired.
    ensure_fresh_token();

    auto* handle = static_cast<::CURL*>(curl_);

    // Build URL.
    auto url = std::format("{}/v1/messages", config_.base_url);

    // Build JSON body.
    auto body = request.to_json().dump();
    std::cerr << "[debug] request body: " << body.substr(0, 1000) << "\n";

    // Build headers.
    auto auth_header = std::format("{}: {}",
        config_.credentials.header_name(),
        config_.credentials.header_value());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers,
        std::format("anthropic-version: {}", config_.api_version).c_str());
    // oauth-2025-04-20 is required when authenticating via OAuth Bearer token.
    if (config_.credentials.method == auth::AuthMethod::OAuthToken) {
        headers = curl_slist_append(headers, "anthropic-beta: oauth-2025-04-20");
    }
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    // Set up context.
    StreamContext ctx;
    ctx.parser.reset();
    ctx.sink = std::ref(sink);

    // Configure curl.
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT,
        static_cast<long>(config_.timeout.count()));
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

    // Perform request.
    CURLcode res = curl_easy_perform(handle);

    // Get HTTP status before reset.
    long http_code = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);

    // Cleanup.
    curl_slist_free_all(headers);
    curl_easy_reset(handle);

    if (res != CURLE_OK) {
        if (ctx.had_error) {
            return make_error(ErrorCode::SseParseError, ctx.error_msg);
        }
        if (res == CURLE_OPERATION_TIMEDOUT) {
            return make_error(ErrorCode::HttpTimeout, "request timed out");
        }
        return make_error(ErrorCode::HttpConnectionFailed,
            std::format("curl error: {}", curl_easy_strerror(res)));
    }

    if (http_code == 401) {
        return make_error(ErrorCode::ApiAuthError,
            "authentication failed (401). Run 'clawed login' to re-authenticate.");
    }

    if (http_code >= 400) {
        // Include the response body for diagnostics (truncated).
        auto body_preview = ctx.raw_body.substr(0, 500);
        return make_error(ErrorCode::ApiError,
            std::format("API error (HTTP {}): {}", http_code, body_preview));
    }

    return {};
}

} // namespace clawed
