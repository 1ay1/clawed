#include <clawed/agent/loop.hpp>
#include <nlohmann/json.hpp>
#include <format>
#include <thread>
#include <future>

namespace clawed {

AgentLoop::AgentLoop(ApiClient& client, ToolRegistry& registry, Config config)
    : client_(client)
    , registry_(registry)
    , config_(std::move(config))
{
    if (!config_.system_prompt.empty()) {
        conversation_.set_system_prompt(config_.system_prompt);
    }
}

auto AgentLoop::run_turn(std::string user_message, UiSink ui_sink) -> Result<void> {
    stop_requested_.store(false);
    conversation_.add_user_message(std::move(user_message));
    sm_.process(event::UserMessage{""}, ui_sink);

    // Run step loop: API call → tool execution → repeat until done.
    while (true) {
        if (stop_requested_.load()) {
            return make_error(ErrorCode::InvalidState, "stop requested");
        }

        auto needs_more = step(ui_sink);
        if (!needs_more) {
            return std::unexpected(needs_more.error());
        }
        if (!*needs_more) {
            break; // Done — model said EndTurn.
        }
    }

    return {};
}

auto AgentLoop::step(UiSink& ui) -> Result<bool> {
    auto request = conversation_.build_request(registry_, config_.model);

    // Collect content blocks and tool calls during streaming.
    struct StreamState {
        std::string         text_accum;
        std::string         tool_json_accum;
        std::vector<api::ContentBlock> content_blocks;

        // Current tool call being assembled.
        std::string current_tool_id;
        std::string current_tool_name;
        bool        in_tool_block    = false;
        bool        in_thinking_block = false;

        StopReason  stop_reason = StopReason::EndTurn;

        // Collected tool calls for execution.
        std::vector<state::ToolExec::PendingCall> tool_calls;
    } ss;

    SseParser::EventSink sink = [&](event::Event evt) {
        std::visit(Overloaded{
            [&](event::ApiResponseStarted& e) {
                sm_.process(std::move(e), ui);
            },
            [&](event::ContentBlockStart& e) {
                if (e.type == ContentType::ToolUse) {
                    ss.current_tool_id   = e.tool_use_id;
                    ss.current_tool_name = e.tool_name;
                    ss.in_tool_block     = true;
                    ss.tool_json_accum.clear();
                } else if (e.type == ContentType::Thinking) {
                    ss.in_thinking_block = true;
                }
                sm_.process(std::move(e), ui);
            },
            [&](event::TextDelta& e) {
                if (!ss.in_thinking_block) {
                    ss.text_accum += e.text;
                }
                sm_.process(std::move(e), ui);
            },
            [&](event::InputJsonDelta& e) {
                ss.tool_json_accum += e.partial_json;
                sm_.process(std::move(e), ui);
            },
            [&](event::ContentBlockStop& e) {
                if (ss.in_thinking_block) {
                    ss.in_thinking_block = false;
                } else if (ss.in_tool_block) {
                    ss.tool_calls.push_back({
                        .id         = ss.current_tool_id,
                        .name       = ss.current_tool_name,
                        .input_json = ss.tool_json_accum
                    });

                    // Add to content blocks for conversation history.
                    nlohmann::json tool_input;
                    try {
                        tool_input = nlohmann::json::parse(ss.tool_json_accum);
                    } catch (...) {
                        tool_input = nlohmann::json::object();
                    }
                    ss.content_blocks.emplace_back(api::ToolUseContent{
                        ss.current_tool_id, ss.current_tool_name, tool_input});

                    ss.in_tool_block = false;
                    ss.tool_json_accum.clear();
                }
                sm_.process(std::move(e), ui);
            },
            [&](event::MessageComplete& e) {
                ss.stop_reason = e.reason;
                // Add accumulated text as content block.
                if (!ss.text_accum.empty()) {
                    ss.content_blocks.emplace_back(
                        api::TextContent{ss.text_accum});
                }
                sm_.process(std::move(e), ui);
            },
            [&](event::ErrorOccurred& e) {
                sm_.process(std::move(e), ui);
            },
            [&](auto&) {
                // Ignore other events during streaming.
            }
        }, evt);
    };

    // Make the API call (blocks until stream completes).
    auto result = client_.stream_message(request, sink);
    if (!result) {
        return std::unexpected(result.error());
    }

    // Record assistant response in conversation.
    if (!ss.content_blocks.empty()) {
        conversation_.add_assistant_response(std::move(ss.content_blocks));
    }

    // If the model wants tool use, execute tools and feed results back.
    if (ss.stop_reason == StopReason::ToolUse && !ss.tool_calls.empty()) {
        state::ToolExec exec_state{.calls = std::move(ss.tool_calls)};
        auto exec_result = execute_tools(exec_state, ui);
        if (!exec_result) {
            return std::unexpected(exec_result.error());
        }
        return true; // Need another API round-trip.
    }

    return false; // Done.
}

auto AgentLoop::execute_tools(state::ToolExec& exec_state, UiSink& ui)
    -> Result<void>
{
    std::vector<std::tuple<ToolUseId, std::string, bool>> results;

    for (const auto& call : exec_state.calls) {
        nlohmann::json input;
        try {
            input = nlohmann::json::parse(call.input_json);
        } catch (const nlohmann::json::exception& ex) {
            results.emplace_back(call.id,
                std::format("JSON parse error: {}", ex.what()), true);
            ui(UiToolEnd{call.id,
                std::format("JSON parse error: {}", ex.what()), true});
            continue;
        }

        auto tool_result = registry_.execute(call.name, input);
        if (tool_result) {
            results.emplace_back(call.id, *tool_result, false);
            ui(UiToolEnd{call.id, *tool_result, false});
        } else {
            auto err_msg = tool_result.error().formatted();
            results.emplace_back(call.id, err_msg, true);
            ui(UiToolEnd{call.id, err_msg, true});
        }
    }

    conversation_.add_tool_results(results);
    return {};
}

} // namespace clawed
