#include <clawed/api/client.hpp>
#include <clawed/auth/auth.hpp>
#include <clawed/tool/registry.hpp>
#include <clawed/tool/builtin/bash.hpp>
#include <clawed/tool/builtin/read_file.hpp>
#include <clawed/tool/builtin/write_file.hpp>
#include <clawed/tool/builtin/glob.hpp>
#include <clawed/agent/loop.hpp>
#include <clawed/tui/app.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_usage() {
    std::cout <<
        "\033[1;36mclawed\033[0m — fast C++ Claude agent\n\n"
        "Usage:\n"
        "  clawed                     Start interactive session\n"
        "  clawed login               Authenticate (browser or API key)\n"
        "  clawed logout              Remove saved credentials\n"
        "  clawed status              Show auth status\n"
        "  clawed -h, --help          Show this help\n\n"
        "Environment:\n"
        "  ANTHROPIC_API_KEY          API key (overrides saved credentials)\n"
        "  CLAWED_MODEL               Model override (default: claude-sonnet-4-20250514)\n";
}

auto build_system_prompt() -> std::string {
    auto cwd = std::filesystem::current_path().string();
    return
        "You are clawed, a fast C++ Claude agent runtime. "
        "You have access to tools for running bash commands, reading files, "
        "writing files, and searching with glob patterns. "
        "Current working directory: " + cwd + "\n"
        "Be concise and direct. Execute tasks efficiently.";
}

auto ensure_auth() -> clawed::auth::Credentials {
    auto creds = clawed::auth::load_credentials();
    if (creds.is_valid()) return creds;

    // No credentials found — run interactive login.
    auto result = clawed::auth::login_interactive();
    if (!result) {
        std::cerr << "\033[31mAuthentication required. "
                  << "Run 'clawed login' to set up.\033[0m\n";
        std::exit(1);
    }
    return *result;
}

int run_agent(const clawed::auth::Credentials& creds) {
    clawed::ApiClient client({
        .credentials = creds,
    });

    clawed::ToolRegistry registry;
    registry.register_tool(clawed::builtin::BashTool{});
    registry.register_tool(clawed::builtin::ReadFileTool{});
    registry.register_tool(clawed::builtin::WriteFileTool{});
    registry.register_tool(clawed::builtin::GlobTool{});

    clawed::AgentLoop::Config agent_config{
        .model         = "claude-sonnet-4-20250514",
        .system_prompt = build_system_prompt(),
    };

    if (auto* model = std::getenv("CLAWED_MODEL")) {
        agent_config.model = model;
    }

    clawed::tui::App app(client, registry, std::move(agent_config));
    app.run();
    return 0;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> args(argv + 1, argv + argc);

    // Subcommand routing.
    if (!args.empty()) {
        auto cmd = args[0];

        if (cmd == "login")                    return clawed::auth::cmd_login();
        if (cmd == "logout")                   return clawed::auth::cmd_logout();
        if (cmd == "status")                   return clawed::auth::cmd_status();
        if (cmd == "-h" || cmd == "--help") { print_usage(); return 0; }
    }

    // Default: interactive agent session.
    auto creds = ensure_auth();
    return run_agent(creds);
}
