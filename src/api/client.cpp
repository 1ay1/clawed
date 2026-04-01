#include <clawed/api/client.hpp>

namespace clawed {

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
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}

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

} // namespace clawed
