#include <clawed/agent/loop.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <format>

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
    sm_.reset();
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

        // Current block tracking.
        std::string current_tool_id;
        std::string current_tool_name;
        bool        in_tool_block     = false;
        bool        in_thinking_block = false;
        bool        in_text_block     = false;

        StopReason  stop_reason = StopReason::EndTurn;

        // Collected tool calls for execution.
        std::vector<state::ToolExec::PendingCall> tool_calls;
    } ss;

    auto sink = [&](event::Event evt) {
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
                } else if (e.type == ContentType::Text) {
                    ss.in_text_block = true;
                    ss.text_accum.clear();
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
                } else if (ss.in_text_block) {
                    // Flush text block at its natural position (before tool_use blocks).
                    if (!ss.text_accum.empty()) {
                        ss.content_blocks.emplace_back(
                            api::TextContent{ss.text_accum});
                    }
                    ss.in_text_block = false;
                }
                sm_.process(std::move(e), ui);
            },
            [&](event::MessageComplete& e) {
                ss.stop_reason = e.reason;
                // Flush remaining text only if still in a text block
                // (i.e., ContentBlockStop hasn't flushed it yet).
                if (ss.in_text_block && !ss.text_accum.empty()) {
                    ss.content_blocks.emplace_back(
                        api::TextContent{ss.text_accum});
                }
                ss.text_accum.clear();
                ss.in_text_block = false;
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

    auto result = client_.stream_message(request, sink);
    if (!result) {
        return std::unexpected(result.error());
    }

    // Record assistant response in conversation.
    // Always add, even if empty — keeps user/assistant message alternation valid.
    conversation_.add_assistant_response(std::move(ss.content_blocks));

    // If the model wants tool use, execute tools and feed results back.
    if (ss.stop_reason == StopReason::ToolUse) {
        if (ss.tool_calls.empty()) {
            // Edge case: stop_reason=ToolUse but no tool_calls collected.
            // Add empty tool results so the conversation stays valid.
            conversation_.add_tool_results({});
        } else {
            state::ToolExec exec_state{.calls = std::move(ss.tool_calls)};
            auto exec_result = execute_tools(exec_state, ui);
            if (!exec_result) {
                return std::unexpected(exec_result.error());
            }
        }
        // Reset SM to Idle so the next step starts clean.
        sm_.reset();
        sm_.process(event::UserMessage{""}, ui);
        return true; // Need another API round-trip.
    }

    return false; // Done.
}

namespace {

/// Summarize tool input for display — shows what the tool is actually doing.
auto summarize_input(const std::string& name, const nlohmann::json& input) -> std::string {
    auto truncate = [](const std::string& s, size_t max = 60) -> std::string {
        if (s.size() <= max) return s;
        return s.substr(0, max) + "...";
    };

    if (name == "bash" && input.contains("command"))
        return truncate(input["command"].get<std::string>(), 80);
    if (name == "read_file" && input.contains("file_path"))
        return input["file_path"].get<std::string>();
    if (name == "write_file" && input.contains("file_path"))
        return input["file_path"].get<std::string>();
    if (name == "glob" && input.contains("pattern"))
        return input["pattern"].get<std::string>();
    if (name == "grep" && input.contains("pattern")) {
        auto s = input["pattern"].get<std::string>();
        if (input.contains("path")) s += " in " + input["path"].get<std::string>();
        return truncate(s);
    }
    if (name == "edit" && input.contains("file_path"))
        return input["file_path"].get<std::string>();
    return {};
}

} // anonymous namespace

auto AgentLoop::execute_tools(state::ToolExec& exec_state, UiSink& ui)
    -> Result<void>
{
    std::vector<std::tuple<ToolUseId, std::string, bool>> results;

    for (const auto& call : exec_state.calls) {
        nlohmann::json input;
        try {
            input = nlohmann::json::parse(call.input_json);
        } catch (const nlohmann::json::exception& ex) {
            auto msg = std::format("JSON parse error: {}", ex.what());
            results.emplace_back(call.id, msg, true);
            ui(UiToolEnd{call.id, msg, true, 0});
            continue;
        }

        // Tell the UI what we're about to do
        auto summary = summarize_input(call.name, input);

        // For edit tool, build a diff detail
        std::string detail;
        if (call.name == "edit" && input.contains("old_string") && input.contains("new_string")) {
            auto old_s = input["old_string"].get<std::string>();
            auto new_s = input["new_string"].get<std::string>();
            if (old_s.size() > 200) old_s = old_s.substr(0, 200) + "...";
            if (new_s.size() > 200) new_s = new_s.substr(0, 200) + "...";
            detail = "- " + old_s + "\n+ " + new_s;
        } else if (call.name == "bash" && input.contains("command")) {
            detail = input["command"].get<std::string>();
        }

        ui(UiToolRunning{call.name, call.id, summary, detail});

        // Execute with timing
        auto t0 = std::chrono::steady_clock::now();
        auto tool_result = registry_.execute(call.name, input);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        if (tool_result) {
            results.emplace_back(call.id, *tool_result, false);
            ui(UiToolEnd{call.id, *tool_result, false, static_cast<int>(ms)});
        } else {
            auto err_msg = tool_result.error().message;
            results.emplace_back(call.id, err_msg, true);
            ui(UiToolEnd{call.id, err_msg, true, static_cast<int>(ms)});
        }
    }

    conversation_.add_tool_results(results);
    return {};
}

} // namespace clawed
