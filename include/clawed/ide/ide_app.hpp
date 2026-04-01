#pragma once

#include <clawed/agent/loop.hpp>
#include <clawed/api/client.hpp>
#include <clawed/tool/registry.hpp>

#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace clawed::ide {

class IdeApp {
public:
    IdeApp(ApiClient& client, ToolRegistry& registry,
           AgentLoop::Config agent_config);

    void run();

private:
    ApiClient&    client_;
    ToolRegistry& registry_;
    AgentLoop     agent_;

    // ── Shared state (agent thread → UI thread) ─────────────────────────
    struct SharedState {
        mutable std::mutex mu;

        // Chat
        std::vector<std::string> chat_lines;
        std::string streaming_text;
        bool is_streaming = false;

        // Tools
        struct ToolInfo {
            std::string name, id, summary;
            enum { Queued, Running, Done, Failed } state = Queued;
            int duration_ms = 0;
        };
        std::vector<ToolInfo> tools;

        // Status
        std::string agent_status = "idle";

        // Code view
        std::string current_file;
        std::vector<std::string> file_lines;

        // Dirty flag
        bool dirty = false;
    };

    SharedState state_;
    std::jthread agent_thread_;

    void submit_message(const std::string& msg);
    auto build_ui_sink() -> UiSink;
};

} // namespace clawed::ide
