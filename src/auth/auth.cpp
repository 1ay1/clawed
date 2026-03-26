#include <clawed/auth/auth.hpp>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <random>
#include <string>
#include <termios.h>
#include <unistd.h>

// Sockets for local HTTP callback server
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

// OpenSSL for SHA-256 (PKCE)
#include <openssl/sha.h>
#include <openssl/evp.h>

namespace clawed::auth {

// ── Helpers ─────────────────────────────────────────────────────────────────

namespace {

auto trim(std::string s) -> std::string {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    while (!s.empty() && (s.front() == '\n' || s.front() == '\r' || s.front() == ' '))
        s.erase(s.begin());
    return s;
}

auto read_hidden_input() -> std::string {
    struct termios old_term{}, new_term{};
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

    std::string input;
    std::getline(std::cin, input);

    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    std::cout << "\n";
    return trim(input);
}

auto try_open_browser(const std::string& url) -> bool {
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) {
        return false;
    }
    auto try_open = [&](const char* cmd) {
        auto full = std::format("{} '{}' >/dev/null 2>&1 &", cmd, url);
        return std::system(full.c_str()) == 0;
    };
    if (try_open("xdg-open")) return true;
    if (try_open("open")) return true;
    if (try_open("wslview")) return true;
    return false;
}

auto base64url_encode(const unsigned char* data, size_t len) -> std::string {
    static constexpr auto table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    result.reserve(4 * ((len + 2) / 3));

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);

        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        if (i + 1 < len) result += table[(n >> 6) & 0x3F];
        if (i + 2 < len) result += table[n & 0x3F];
    }

    // Convert to URL-safe: + → -, / → _, strip =
    for (auto& c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }

    return result;
}

// Simple curl POST helper that returns the response body.
struct CurlResponse {
    long        status = 0;
    std::string body;
};

auto curl_write_string(char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
    auto* str = static_cast<std::string*>(userdata);
    str->append(ptr, size * nmemb);
    return size * nmemb;
}

auto http_post(const std::string& url, const std::string& body,
               const std::string& content_type = "application/x-www-form-urlencoded",
               const std::string& accept = "")
    -> Result<CurlResponse>
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        return make_error(ErrorCode::HttpConnectionFailed, "curl init failed");
    }

    CurlResponse resp;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers,
        std::format("Content-Type: {}", content_type).c_str());
    if (!accept.empty()) {
        headers = curl_slist_append(headers,
            std::format("Accept: {}", accept).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    auto res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return make_error(ErrorCode::HttpConnectionFailed,
            std::format("HTTP request failed: {}", curl_easy_strerror(res)));
    }

    return resp;
}

auto url_encode(const std::string& str) -> std::string {
    char* encoded = curl_easy_escape(nullptr, str.c_str(),
                                     static_cast<int>(str.size()));
    if (!encoded) return str;
    std::string result(encoded);
    curl_free(encoded);
    return result;
}

auto now_ms() -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // anonymous namespace

// ── Credentials ─────────────────────────────────────────────────────────────

auto Credentials::is_expired() const -> bool {
    if (method != AuthMethod::OAuthToken) return false;
    if (expires_at == 0) return false;
    return now_ms() >= expires_at;
}

auto Credentials::header_value() const -> std::string {
    switch (method) {
        case AuthMethod::OAuthToken: return "Bearer " + access_token;
        default:                     return access_token;
    }
}

// ── Config paths ────────────────────────────────────────────────────────────

auto config_dir() -> std::filesystem::path {
    if (auto* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "clawed";
    }
    if (auto* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "clawed";
    }
    return ".clawed";
}

auto config_file() -> std::filesystem::path {
    return config_dir() / "credentials.json";
}

// ── Credential management ───────────────────────────────────────────────────

auto load_credentials() -> Credentials {
    // 1. Environment variable (API key).
    if (auto* key = std::getenv("ANTHROPIC_API_KEY"); key && *key) {
        return {AuthMethod::ApiKey, key, {}, 0};
    }

    // 2. OAuth token env var (like Claude Code supports).
    if (auto* token = std::getenv("CLAUDE_CODE_OAUTH_TOKEN"); token && *token) {
        return {AuthMethod::OAuthToken, token, {}, 0};
    }

    // 3. Saved credentials file.
    auto path = config_file();
    if (!std::filesystem::exists(path)) return {};

    try {
        std::ifstream f(path);
        auto j = nlohmann::json::parse(f);

        Credentials creds;
        auto method = j.value("method", "api_key");
        creds.method        = (method == "oauth") ? AuthMethod::OAuthToken : AuthMethod::ApiKey;
        creds.access_token  = j.value("access_token", "");
        creds.refresh_token = j.value("refresh_token", "");
        creds.expires_at    = j.value("expires_at", int64_t{0});
        return creds;
    } catch (...) {
        return {};
    }
}

void save_credentials(const Credentials& creds) {
    auto dir = config_dir();
    std::filesystem::create_directories(dir);
    std::filesystem::permissions(dir,
        std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace);

    nlohmann::json j;
    j["method"]        = (creds.method == AuthMethod::OAuthToken) ? "oauth" : "api_key";
    j["access_token"]  = creds.access_token;
    j["refresh_token"] = creds.refresh_token;
    j["expires_at"]    = creds.expires_at;

    auto path = config_file();
    std::ofstream f(path, std::ios::trunc);
    f << j.dump(2) << "\n";
    f.close();

    std::filesystem::permissions(path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);
}

void clear_credentials() {
    auto path = config_file();
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}

// ── PKCE ────────────────────────────────────────────────────────────────────

auto generate_code_verifier() -> std::string {
    static constexpr auto charset =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    static constexpr size_t len = 128;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, 65); // charset length - 1

    std::string verifier;
    verifier.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        verifier += charset[dist(gen)];
    }
    return verifier;
}

auto generate_code_challenge(const std::string& verifier) -> std::string {
    // SHA-256 hash.
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(verifier.data()),
           verifier.size(), hash);

    // Base64url encode.
    return base64url_encode(hash, SHA256_DIGEST_LENGTH);
}

// ── Token exchange ──────────────────────────────────────────────────────────

auto exchange_code_for_token(const std::string& auth_code,
                             const std::string& code_verifier,
                             const std::string& redirect_uri,
                             const std::string& state)
    -> Result<Credentials>
{
    // Strip #fragment (state) from pasted code if present.
    auto code = auth_code;
    auto hash_pos = code.find('#');
    if (hash_pos != std::string::npos) {
        code = code.substr(0, hash_pos);
    }

    // Build JSON body (Claude's token endpoint expects JSON, not form-encoded).
    nlohmann::json body_json = {
        {"grant_type",    "authorization_code"},
        {"code",          code},
        {"redirect_uri",  redirect_uri},
        {"client_id",     OAuthConfig::client_id},
        {"code_verifier", code_verifier},
        {"state",         state}
    };

    auto resp = http_post(OAuthConfig::token_url, body_json.dump(),
                          "application/json", "application/json");
    if (!resp) return std::unexpected(resp.error());

    if (resp->status != 200) {
        return make_error(ErrorCode::ApiAuthError,
            std::format("token exchange failed (HTTP {}): {}", resp->status, resp->body));
    }

    try {
        auto j = nlohmann::json::parse(resp->body);

        Credentials creds;
        creds.method        = AuthMethod::OAuthToken;
        creds.access_token  = j.value("access_token", "");
        creds.refresh_token = j.value("refresh_token", "");

        auto expires_in = j.value("expires_in", 3600);
        creds.expires_at = now_ms() + (static_cast<int64_t>(expires_in) * 1000);

        if (creds.access_token.empty()) {
            return make_error(ErrorCode::ApiAuthError, "no access_token in response");
        }

        return creds;
    } catch (const nlohmann::json::exception& ex) {
        return make_error(ErrorCode::JsonParseError,
            std::format("failed to parse token response: {}", ex.what()));
    }
}

auto refresh_access_token(const std::string& refresh_token) -> Result<Credentials> {
    std::string body;
    body += "grant_type=refresh_token";
    body += "&client_id=" + url_encode(OAuthConfig::client_id);
    body += "&refresh_token=" + url_encode(refresh_token);

    auto resp = http_post(OAuthConfig::token_url, body);
    if (!resp) return std::unexpected(resp.error());

    if (resp->status != 200) {
        return make_error(ErrorCode::ApiAuthError,
            std::format("token refresh failed (HTTP {}): {}", resp->status, resp->body));
    }

    try {
        auto j = nlohmann::json::parse(resp->body);

        Credentials creds;
        creds.method        = AuthMethod::OAuthToken;
        creds.access_token  = j.value("access_token", "");
        creds.refresh_token = j.value("refresh_token", refresh_token); // keep old if not returned
        auto expires_in = j.value("expires_in", 3600);
        creds.expires_at = now_ms() + (static_cast<int64_t>(expires_in) * 1000);

        return creds;
    } catch (const nlohmann::json::exception& ex) {
        return make_error(ErrorCode::JsonParseError,
            std::format("failed to parse refresh response: {}", ex.what()));
    }
}

// ── Login flows ─────────────────────────────────────────────────────────────

auto login_with_key(std::string key) -> Result<Credentials> {
    key = trim(key);
    if (key.empty()) {
        return make_error(ErrorCode::ConfigError, "empty API key");
    }

    Credentials creds{AuthMethod::ApiKey, std::move(key), {}, 0};
    save_credentials(creds);
    return creds;
}

auto login_interactive() -> Result<Credentials> {
    std::cout << "\n\033[1;36m  clawed\033[0m — authentication\n\n";
    std::cout << "  How would you like to authenticate?\n\n";
    std::cout << "  \033[1m1.\033[0m Log in with Claude   \033[90m(uses your Pro/Max subscription)\033[0m\n";
    std::cout << "  \033[1m2.\033[0m Use an API key       \033[90m(uses API billing)\033[0m\n\n";
    std::cout << "  Choice [1]: " << std::flush;

    std::string choice;
    std::getline(std::cin, choice);
    choice = trim(choice);

    if (choice == "2") {
        // API key flow.
        std::cout << "\n  Paste your API key (from https://console.anthropic.com/settings/keys)\n";
        std::cout << "  \033[1mAPI key:\033[0m " << std::flush;
        auto key = read_hidden_input();
        if (key.empty()) {
            return make_error(ErrorCode::ConfigError, "no key provided");
        }
        auto creds = login_with_key(std::move(key));
        if (creds) {
            std::cout << "  \033[32mAuthenticated!\033[0m\n\n";
        }
        return creds;
    }

    // ── OAuth flow (default) ────────────────────────────────────────────────
    auto code_verifier  = generate_code_verifier();
    auto code_challenge = generate_code_challenge(code_verifier);
    auto state          = generate_code_verifier().substr(0, 32);

    // Use the manual redirect URL (the only one the OAuth client allows).
    std::string redirect_uri = OAuthConfig::callback_url;

    // Build authorization URL.
    std::string auth_url = OAuthConfig::authorize_url;
    auth_url += "?code=true";
    auth_url += "&client_id=" + url_encode(OAuthConfig::client_id);
    auth_url += "&response_type=code";
    auth_url += "&redirect_uri=" + url_encode(redirect_uri);
    auth_url += "&scope=" + url_encode(OAuthConfig::scopes);
    auth_url += "&code_challenge=" + url_encode(code_challenge);
    auth_url += "&code_challenge_method=S256";
    auth_url += "&state=" + url_encode(state);

    // Always print the URL (works over SSH).
    std::cout << "\n  Open this URL in your browser to log in:\n\n";
    std::cout << "    \033[4;34m" << auth_url << "\033[0m\n\n";

    if (try_open_browser(auth_url)) {
        std::cout << "  \033[90m(browser opened)\033[0m\n";
    }

    std::cout << "\n  After logging in, paste the authorization code shown on the page.\n\n";
    std::cout << "  \033[1mPaste code here:\033[0m " << std::flush;

    std::string auth_code;
    std::getline(std::cin, auth_code);
    auth_code = trim(auth_code);

    if (auth_code.empty()) {
        return make_error(ErrorCode::ConfigError, "no authorization code provided");
    }

    std::cout << "\n  Exchanging code for token..." << std::flush;
    auto creds = exchange_code_for_token(auth_code, code_verifier, redirect_uri, state);

    if (!creds) {
        std::cout << " \033[31mfailed\033[0m\n";
        std::cerr << "  " << creds.error().message << "\n";
        return creds;
    }

    save_credentials(*creds);
    std::cout << " \033[32mdone!\033[0m\n";
    std::cout << "  \033[32mAuthenticated!\033[0m Credentials saved to "
              << config_file().string() << "\n\n";

    return creds;
}

// ── Subcommands ─────────────────────────────────────────────────────────────

int cmd_login() {
    auto result = login_interactive();
    if (!result) {
        std::cerr << std::format("\033[31m  Login failed: {}\033[0m\n",
                                 result.error().message);
        return 1;
    }
    return 0;
}

int cmd_logout() {
    auto creds = load_credentials();
    if (!creds.is_valid()) {
        std::cout << "Not logged in.\n";
        return 0;
    }

    clear_credentials();
    std::cout << "Logged out. Credentials removed.\n";
    return 0;
}

int cmd_status() {
    auto creds = load_credentials();
    if (!creds.is_valid()) {
        std::cout << "\033[33mNot authenticated.\033[0m Run \033[1mclawed login\033[0m to set up.\n";
        return 1;
    }

    auto method_str = (creds.method == AuthMethod::OAuthToken) ? "Claude login (OAuth)" : "API key";
    auto masked = creds.access_token.substr(0, 12) + "..." +
                  creds.access_token.substr(
                      creds.access_token.size() > 6 ? creds.access_token.size() - 4 : 0);

    std::cout << std::format("\033[32mAuthenticated\033[0m via {}\n", method_str);
    std::cout << std::format("Token: {}\n", masked);

    if (creds.method == AuthMethod::OAuthToken && creds.expires_at > 0) {
        auto remaining_s = (creds.expires_at - now_ms()) / 1000;
        if (remaining_s > 0) {
            auto hours = remaining_s / 3600;
            auto mins  = (remaining_s % 3600) / 60;
            std::cout << std::format("Expires in: {}h {}m\n", hours, mins);
        } else {
            std::cout << "\033[33mToken expired\033[0m (will refresh on next request)\n";
        }
    }

    std::cout << std::format("Stored: {}\n", config_file().string());
    return 0;
}

} // namespace clawed::auth
