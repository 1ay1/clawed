#pragma once

#include <clawed/agent/loop.hpp>
#include <clawed/api/client.hpp>
#include <clawed/tool/registry.hpp>
#include <string>

namespace clawed::tui {

class App {
public:
    App(ApiClient& client, ToolRegistry& registry, AgentLoop::Config agent_config);

    // Run the full TUI application. Blocks until exit.
    void run();

private:
    ApiClient&      client_;
    ToolRegistry&   registry_;
    AgentLoop       agent_;

    void run_interactive();
    void run_oneshot(const std::string& prompt);
};

} // namespace clawed::tui
