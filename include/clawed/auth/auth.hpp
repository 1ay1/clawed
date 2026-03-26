#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <filesystem>
#include <string>

namespace clawed::auth {

enum class AuthMethod : uint8_t {
    ApiKey,      // sk-ant-... via env or saved
    OAuthToken,  // OAuth bearer token from claude.ai login
};

struct Credentials {
    AuthMethod  method = AuthMethod::ApiKey;
    std::string access_token;
    std::string refresh_token;
    int64_t     expires_at = 0; // unix ms

    [[nodiscard]] auto is_valid() const -> bool { return !access_token.empty(); }
    [[nodiscard]] auto is_expired() const -> bool;

    // Header name depends on auth method.
    [[nodiscard]] auto header_name() const -> std::string_view {
        switch (method) {
            case AuthMethod::OAuthToken: return "Authorization";
            default:                     return "x-api-key";
        }
    }

    // Header value depends on auth method.
    [[nodiscard]] auto header_value() const -> std::string;
};

// ── OAuth config ────────────────────────────────────────────────────────────
struct OAuthConfig {
    static constexpr auto client_id              = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
    static constexpr auto authorize_url          = "https://claude.ai/oauth/authorize";
    static constexpr auto console_authorize_url  = "https://platform.claude.com/oauth/authorize";
    static constexpr auto token_url              = "https://platform.claude.com/v1/oauth/token";
    static constexpr auto callback_url           = "https://platform.claude.com/oauth/code/callback";
    static constexpr auto success_url            = "https://platform.claude.com/oauth/code/success?app=claude-code";
    static constexpr auto api_key_url            = "https://api.anthropic.com/api/oauth/claude_cli/create_api_key";
    static constexpr auto scopes                 = "user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload";
};

// ── Config paths ────────────────────────────────────────────────────────────
auto config_dir()  -> std::filesystem::path;
auto config_file() -> std::filesystem::path;

// ── Credential management ───────────────────────────────────────────────────
auto load_credentials() -> Credentials;
void save_credentials(const Credentials& creds);
void clear_credentials();

// ── OAuth PKCE helpers ──────────────────────────────────────────────────────
auto generate_code_verifier() -> std::string;
auto generate_code_challenge(const std::string& verifier) -> std::string;

// ── Token exchange ──────────────────────────────────────────────────────────
auto exchange_code_for_token(const std::string& auth_code,
                             const std::string& code_verifier,
                             const std::string& redirect_uri,
                             const std::string& state)
    -> Result<Credentials>;

auto refresh_access_token(const std::string& refresh_token)
    -> Result<Credentials>;

// ── Login flows ─────────────────────────────────────────────────────────────
auto login_interactive() -> Result<Credentials>;
auto login_with_key(std::string key) -> Result<Credentials>;

// ── Subcommands ─────────────────────────────────────────────────────────────
int cmd_login();
int cmd_logout();
int cmd_status();

} // namespace clawed::auth
