#include <clawed/api/client.hpp>
#include <clawed/auth/auth.hpp>
#include <clawed/tool/registry.hpp>
#include <clawed/tool/unified.hpp>
#include <clawed/executor/concept.hpp>
#include <clawed/executor/local.hpp>
#include <clawed/executor/ssh.hpp>
#include <clawed/ssh/session.hpp>
#include <clawed/agent/loop.hpp>
#include <clawed/tui/app.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_usage() {
    std::cout <<
        "\033[1;36mclawed\033[0m — fast C++ Claude agent\n\n"
        "Usage:\n"
        "  clawed                              Start interactive session\n"
        "  clawed remote user@host [dir]       Work on a remote machine via SSH\n"
        "  clawed login                        Authenticate (browser or API key)\n"
        "  clawed logout                       Remove saved credentials\n"
        "  clawed status                       Show auth status\n"
        "  clawed -h, --help                   Show this help\n\n"
        "Environment:\n"
        "  ANTHROPIC_API_KEY          API key (overrides saved credentials)\n"
        "  CLAWED_MODEL               Model override (default: claude-haiku-4-5-20251001)\n";
}

auto build_core_prompt() -> std::string {
    return R"(You are clawed, a fast C++ agent runtime. You help with software engineering tasks.

# SPEED — this is critical
- ALWAYS call multiple tools in parallel when they are independent. NEVER serialize independent tool calls.
- Minimize API round-trips. Gather as much info as possible in ONE batch of parallel tool calls.
- Do not read files one at a time. Read all the files you need in a single batch.
- Do not explain what you're about to do. Just do it.

# Tools
You have: bash, read_file, write_file, glob, grep, edit.

- read_file: reads files with line numbers. If given a directory, lists its contents.
- write_file: creates/overwrites files, creates parent dirs.
- edit: surgical find-and-replace in a file. old_string must be unique.
- bash: run any shell command. Use for: git, build, test, install, ls, etc.
- glob: find files by pattern. Supports ** for recursive.
- grep: search file contents with regex. Uses ripgrep if available.

# Tool strategy
- Use glob/grep FIRST to find what you need. Only then read_file specific files.
- Prefer edit over write_file for changes to existing files.
- Prefer grep over bash with grep. Prefer read_file over bash with cat.

# Output
- Extremely concise. No filler. No preamble. No "Let me..." or "I'll...".
- Do not restate the question. Lead with the answer or action.
- Do not add commentary, suggestions, or follow-ups unless asked.

# Code changes
- Read before modifying. Match existing style exactly.
- Do not add features, refactoring, comments, or error handling beyond what was asked.
)";
}

auto build_system_prompt() -> std::string {
    auto cwd = std::filesystem::current_path().string();
    return build_core_prompt() +
        "# Environment\n"
        "Working directory: " + cwd + "\n"
        "Platform: " +
#ifdef __linux__
        "Linux"
#elif __APPLE__
        "macOS"
#else
        "Unknown"
#endif
        "\n";
}

auto build_remote_system_prompt(const std::string& host,
                                const std::string& dir) -> std::string {
    return build_core_prompt() +
        "# Environment\n"
        "REMOTE host via SSH: " + host + "\n"
        "Remote working directory: " + dir + "\n"
        "All tools execute on the remote host. File paths are relative to the remote working directory.\n";
}

auto get_model() -> std::string {
    if (auto* model = std::getenv("CLAWED_MODEL")) return model;
    return "claude-haiku-4-5-20251001";
}

auto ensure_auth() -> clawed::auth::Credentials {
    auto creds = clawed::auth::load_credentials();
    if (creds.is_valid()) return creds;

    auto result = clawed::auth::login_interactive();
    if (!result) {
        std::cerr << "\033[31mAuthentication required. "
                  << "Run 'clawed login' to set up.\033[0m\n";
        std::exit(1);
    }
    return *result;
}

// ── Launch agent with a given executor ───────────────────────────────────────

int launch(const clawed::auth::Credentials& creds,
           std::shared_ptr<clawed::AnyExecutor> executor,
           std::string system_prompt) {
    clawed::ApiClient client({.credentials = creds});

    clawed::ToolRegistry registry(std::move(executor));
    clawed::tools::register_all(registry);

    clawed::AgentLoop::Config agent_config{
        .model         = get_model(),
        .system_prompt = std::move(system_prompt),
    };

    clawed::tui::App app(client, registry, std::move(agent_config));
    app.run();
    return 0;
}

// ── Local mode ───────────────────────────────────────────────────────────────

int run_agent(const clawed::auth::Credentials& creds) {
    auto executor = std::make_shared<clawed::AnyExecutor>(clawed::LocalExecutor{});
    return launch(creds, std::move(executor), build_system_prompt());
}

// ── Remote mode ──────────────────────────────────────────────────────────────

auto pick_remote_directory(clawed::ssh::SshSession& session) -> std::string {
    auto home = session.run("echo $HOME", std::chrono::seconds{5});
    std::string home_dir = "/home";
    if (home) {
        home_dir = *home;
        while (!home_dir.empty() && (home_dir.back() == '\n' || home_dir.back() == '\r'))
            home_dir.pop_back();
    }

    auto listing = session.run(
        "ls -1d " + clawed::ssh::shell_escape(home_dir) + "/*/ 2>/dev/null | head -20",
        std::chrono::seconds{5});

    std::cout << "\n\033[1;36mConnected to " << session.host() << "\033[0m\n\n";

    std::vector<std::string> dirs;
    if (listing && !listing->empty()) {
        std::istringstream iss(*listing);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) {
                if (line.back() == '/') line.pop_back();
                dirs.push_back(line);
            }
        }

        if (!dirs.empty()) {
            std::cout << "Directories in " << home_dir << ":\n";
            for (size_t i = 0; i < dirs.size(); ++i)
                std::cout << "  [" << (i + 1) << "] " << dirs[i] << "\n";
            std::cout << "\nEnter number or absolute path: ";
        } else {
            std::cout << "Enter remote working directory: ";
        }
    } else {
        std::cout << "Enter remote working directory: ";
    }

    std::string input;
    std::getline(std::cin, input);

    while (!input.empty() && (input.back() == '\n' || input.back() == '\r' || input.back() == ' '))
        input.pop_back();
    while (!input.empty() && input.front() == ' ')
        input.erase(input.begin());

    if (input.empty()) return home_dir;

    bool is_num = !input.empty();
    for (char c : input) {
        if (!std::isdigit(c)) { is_num = false; break; }
    }

    if (is_num && !dirs.empty()) {
        int idx = std::stoi(input) - 1;
        if (idx >= 0 && idx < static_cast<int>(dirs.size()))
            return dirs[static_cast<size_t>(idx)];
    }

    return input;
}

int run_remote_agent(const clawed::auth::Credentials& creds,
                     const std::string& host,
                     std::string remote_dir) {
    auto session = std::make_shared<clawed::ssh::SshSession>(
        clawed::ssh::SshSession::Config{.host = host, .remote_dir = {}});

    std::cout << "\033[90mConnecting to " << host << "...\033[0m\n";

    auto conn_result = session->connect();
    if (!conn_result) {
        std::cerr << "\033[31mSSH connection failed: "
                  << conn_result.error().message << "\033[0m\n";
        return 1;
    }

    if (remote_dir.empty())
        remote_dir = pick_remote_directory(*session);

    session->set_remote_dir(remote_dir);

    std::cout << "\033[90mWorking in " << remote_dir << " on " << host << "\033[0m\n\n";

    auto executor = std::make_shared<clawed::AnyExecutor>(
        clawed::SshExecutor{session});

    return launch(creds, std::move(executor),
                  build_remote_system_prompt(host, remote_dir));
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> args(argv + 1, argv + argc);

    if (!args.empty()) {
        auto cmd = args[0];

        if (cmd == "login")                    return clawed::auth::cmd_login();
        if (cmd == "logout")                   return clawed::auth::cmd_logout();
        if (cmd == "status")                   return clawed::auth::cmd_status();
        if (cmd == "-h" || cmd == "--help") { print_usage(); return 0; }

        if (cmd == "remote") {
            if (args.size() < 2) {
                std::cerr << "Usage: clawed remote user@host [directory]\n";
                return 1;
            }
            auto host = std::string(args[1]);
            std::string dir;
            if (args.size() >= 3) dir = std::string(args[2]);
            auto creds = ensure_auth();
            return run_remote_agent(creds, host, dir);
        }
    }

    auto creds = ensure_auth();
    return run_agent(creds);
}
