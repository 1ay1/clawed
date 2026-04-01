#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <clawed/core/state_machine.hpp>
#include <clawed/api/client.hpp>
#include <clawed/agent/conversation.hpp>
#include <clawed/tool/registry.hpp>
#include <atomic>
#include <string>

namespace clawed {

class AgentLoop {
public:
    struct Config {
        std::string model = "claude-haiku-4-5-20251001";
        std::string system_prompt;
    };

    AgentLoop(ApiClient& client, ToolRegistry& registry, Config config);

    // Run one turn: user message → (possibly multiple) API round-trips → done.
    // Blocks until the model finishes (EndTurn) or an error occurs.
    // UI events are pushed through ui_sink.
    auto run_turn(std::string user_message, UiSink ui_sink) -> Result<void>;

    // Signal the loop to stop after the current API call.
    void request_stop() { stop_requested_.store(true); }

    [[nodiscard]] auto conversation() const -> const Conversation& {
        return conversation_;
    }

private:
    ApiClient&    client_;
    ToolRegistry& registry_;
    Config        config_;
    StateMachine  sm_;
    Conversation  conversation_;
    std::atomic<bool> stop_requested_{false};

    // One API round-trip: send messages, stream response, collect tool calls.
    // Returns true if another round-trip is needed (tool calls to execute).
    auto step(UiSink& ui) -> Result<bool>;

    // Execute all pending tool calls and feed results back into conversation.
    auto execute_tools(state::ToolExec& exec_state, UiSink& ui) -> Result<void>;
};

} // namespace clawed
