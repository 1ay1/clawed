#include <clawed/api/sse_parser.hpp>
#include <nlohmann/json.hpp>
#include <format>

namespace clawed {

void SseParser::feed(std::span<const char> chunk, EventSink& sink) {
    for (size_t i = 0; i < chunk.size(); ++i) {
        char c = chunk[i];

        if (c == '\n') {
            process_line(line_buffer_, sink);
            line_buffer_.clear();
        } else if (c != '\r') {
            line_buffer_ += c;
        }
    }
}

void SseParser::reset() {
    line_buffer_.clear();
    event_type_.clear();
    data_buffer_.clear();
}

void SseParser::process_line(std::string_view line, EventSink& sink) {
    if (line.empty()) {
        // Blank line = end of SSE event.
        if (!data_buffer_.empty()) {
            dispatch_event(sink);
        }
        event_type_.clear();
        data_buffer_.clear();
        return;
    }

    if (line.starts_with("event:")) {
        event_type_ = line.substr(6);
        // Trim leading space.
        if (!event_type_.empty() && event_type_[0] == ' ')
            event_type_ = event_type_.substr(1);
    } else if (line.starts_with("data:")) {
        auto data = line.substr(5);
        if (!data.empty() && data[0] == ' ')
            data = data.substr(1);
        if (!data_buffer_.empty())
            data_buffer_ += '\n';
        data_buffer_ += data;
    }
    // Ignore "id:", "retry:", comments (":"), and unknown fields.
}

void SseParser::dispatch_event(EventSink& sink) {
    auto evt = parse_payload(event_type_, data_buffer_);
    if (evt) {
        sink(std::move(*evt));
    }
}

auto SseParser::parse_payload(std::string_view type, std::string_view data)
    -> std::optional<event::Event>
{
    try {
        auto j = nlohmann::json::parse(data);

        if (type == "message_start") {
            std::string id;
            if (j.contains("message") && j["message"].contains("id")) {
                id = j["message"]["id"].get<std::string>();
            }
            return event::ApiResponseStarted{std::move(id)};
        }

        if (type == "content_block_start") {
            auto idx = j.value("index", size_t{0});
            auto& cb = j["content_block"];
            auto cb_type = cb.value("type", "");

            if (cb_type == "text") {
                return event::ContentBlockStart{
                    idx, ContentType::Text, {}, {}};
            }
            if (cb_type == "tool_use") {
                return event::ContentBlockStart{
                    idx, ContentType::ToolUse,
                    cb.value("id", ""),
                    cb.value("name", "")};
            }
            return std::nullopt;
        }

        if (type == "content_block_delta") {
            auto& delta = j["delta"];
            auto delta_type = delta.value("type", "");

            if (delta_type == "text_delta") {
                return event::TextDelta{delta.value("text", "")};
            }
            if (delta_type == "input_json_delta") {
                return event::InputJsonDelta{delta.value("partial_json", "")};
            }
            return std::nullopt;
        }

        if (type == "content_block_stop") {
            return event::ContentBlockStop{j.value("index", size_t{0})};
        }

        if (type == "message_delta") {
            auto& delta = j["delta"];
            auto reason = delta.value("stop_reason", "");

            StopReason sr = StopReason::EndTurn;
            if (reason == "tool_use")       sr = StopReason::ToolUse;
            else if (reason == "max_tokens") sr = StopReason::MaxTokens;
            else if (reason == "stop_sequence") sr = StopReason::StopSequence;

            return event::MessageComplete{sr};
        }

        if (type == "message_stop") {
            // Already handled by message_delta's stop_reason.
            return std::nullopt;
        }

        if (type == "error") {
            auto msg = j.value("message", "unknown API error");
            if (j.contains("error")) {
                msg = j["error"].value("message", msg);
            }
            return event::ErrorOccurred{
                Error{ErrorCode::ApiError, std::move(msg)}};
        }

        // ping, unknown events → ignore.
        return std::nullopt;

    } catch (const nlohmann::json::exception& ex) {
        return event::ErrorOccurred{
            Error{ErrorCode::JsonParseError,
                  std::format("SSE JSON parse error: {}", ex.what())}};
    }
}

} // namespace clawed
