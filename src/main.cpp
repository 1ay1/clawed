#include <clawed/api/client.hpp>
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

namespace {

auto get_api_key() -> std::string {
    if (auto* key = std::getenv("ANTHROPIC_API_KEY")) {
        return key;
    }
    std::cerr << "Error: ANTHROPIC_API_KEY environment variable not set.\n";
    std::exit(1);
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

} // anonymous namespace

int main(int argc, char* argv[]) {
    // ── API client ──────────────────────────────────────────────────────────
    clawed::ApiClient client({
        .api_key = get_api_key(),
    });

    // ── Tool registry ───────────────────────────────────────────────────────
    clawed::ToolRegistry registry;
    registry.register_tool(clawed::builtin::BashTool{});
    registry.register_tool(clawed::builtin::ReadFileTool{});
    registry.register_tool(clawed::builtin::WriteFileTool{});
    registry.register_tool(clawed::builtin::GlobTool{});

    // ── Agent config ────────────────────────────────────────────────────────
    clawed::AgentLoop::Config agent_config{
        .model         = "claude-sonnet-4-20250514",
        .system_prompt = build_system_prompt(),
    };

    // Check for model override.
    if (auto* model = std::getenv("CLAWED_MODEL")) {
        agent_config.model = model;
    }

    // ── Run ─────────────────────────────────────────────────────────────────
    clawed::tui::App app(client, registry, std::move(agent_config));
    app.run();

    return 0;
}
