#include <clawed/api/sse_parser.hpp>
#include <nlohmann/json.hpp>
#include <format>

namespace clawed {

void SseParser::reset() {
    line_buffer_.clear();
    event_type_.clear();
    data_buffer_.clear();
}

auto SseParser::parse_payload(std::string_view type, std::string_view data)
    -> std::optional<event::Event>
{
    try {
        auto j = nlohmann::json::parse(data);

        if (type == "message_start") {
            std::string id;
            if (j.contains("message") && j["message"].contains("id"))
                id = j["message"]["id"].get<std::string>();
            return event::ApiResponseStarted{std::move(id)};
        }

        if (type == "content_block_start") {
            auto  idx     = j.value("index", size_t{0});
            auto& cb      = j["content_block"];
            auto  cb_type = cb.value("type", "");

            if (cb_type == "text")
                return event::ContentBlockStart{idx, ContentType::Text, {}, {}};
            if (cb_type == "thinking")
                return event::ContentBlockStart{idx, ContentType::Thinking, {}, {}};
            if (cb_type == "tool_use")
                return event::ContentBlockStart{idx, ContentType::ToolUse,
                    cb.value("id", ""), cb.value("name", "")};
            return std::nullopt;
        }

        if (type == "content_block_delta") {
            auto& delta      = j["delta"];
            auto  delta_type = delta.value("type", "");

            if (delta_type == "text_delta")
                return event::TextDelta{delta.value("text", "")};
            if (delta_type == "thinking_delta")
                return event::TextDelta{delta.value("thinking", "")};
            if (delta_type == "input_json_delta")
                return event::InputJsonDelta{delta.value("partial_json", "")};
            return std::nullopt;
        }

        if (type == "content_block_stop")
            return event::ContentBlockStop{j.value("index", size_t{0})};

        if (type == "message_delta") {
            auto reason = j["delta"].value("stop_reason", "");
            StopReason sr = StopReason::EndTurn;
            if (reason == "tool_use")        sr = StopReason::ToolUse;
            else if (reason == "max_tokens") sr = StopReason::MaxTokens;
            else if (reason == "stop_sequence") sr = StopReason::StopSequence;
            return event::MessageComplete{sr};
        }

        if (type == "error") {
            auto msg = j.contains("error")
                ? j["error"].value("message", "unknown API error")
                : j.value("message", "unknown API error");
            return event::ErrorOccurred{Error{ErrorCode::ApiError, std::move(msg)}};
        }

        return std::nullopt; // ping, message_stop, unknown → ignore

    } catch (const nlohmann::json::exception& ex) {
        return event::ErrorOccurred{
            Error{ErrorCode::JsonParseError,
                  std::format("SSE JSON parse error: {}", ex.what())}};
    }
}

} // namespace clawed
