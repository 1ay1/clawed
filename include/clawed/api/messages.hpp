#pragma once

#include <clawed/core/types.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace clawed::api {

// ── Content blocks ──────────────────────────────────────────────────────────

struct TextContent {
    std::string text;
};

struct ToolUseContent {
    ToolUseId       id;
    std::string     name;
    nlohmann::json  input;
};

struct ToolResultContent {
    ToolUseId   tool_use_id;
    std::string content;
    bool        is_error = false;
};

using ContentBlock = std::variant<TextContent, ToolUseContent, ToolResultContent>;

// ── Message ─────────────────────────────────────────────────────────────────

struct Message {
    Role                     role;
    std::vector<ContentBlock> content;
};

// ── Tool definition ─────────────────────────────────────────────────────────

struct ToolDefinition {
    std::string    name;
    std::string    description;
    nlohmann::json input_schema;

    [[nodiscard]] auto to_json() const -> nlohmann::json {
        return {
            {"name",         name},
            {"description",  description},
            {"input_schema", input_schema}
        };
    }
};

// ── Request ─────────────────────────────────────────────────────────────────

struct CreateMessageRequest {
    std::string                  model       = "claude-haiku-4-5-20251001";
    std::vector<Message>         messages;
    std::string                  system_prompt;
    uint32_t                     max_tokens  = 16384;
    bool                         stream      = true;
    std::vector<ToolDefinition>  tools;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// ── JSON serialization ──────────────────────────────────────────────────────

inline void to_json(nlohmann::json& j, const TextContent& c) {
    j = {{"type", "text"}, {"text", c.text}};
}

inline void to_json(nlohmann::json& j, const ToolUseContent& c) {
    j = {{"type", "tool_use"}, {"id", c.id}, {"name", c.name}, {"input", c.input}};
}

inline void to_json(nlohmann::json& j, const ToolResultContent& c) {
    j = {{"type", "tool_result"}, {"tool_use_id", c.tool_use_id},
         {"content", c.content}};
    if (c.is_error) j["is_error"] = true;
}

inline void to_json(nlohmann::json& j, const ContentBlock& block) {
    std::visit([&](const auto& b) { to_json(j, b); }, block);
}

inline void to_json(nlohmann::json& j, const Message& m) {
    j["role"] = (m.role == Role::User) ? "user" : "assistant";
    j["content"] = nlohmann::json::array();
    for (const auto& block : m.content) {
        nlohmann::json bj;
        to_json(bj, block);
        j["content"].push_back(std::move(bj));
    }
}

inline auto CreateMessageRequest::to_json() const -> nlohmann::json {
    nlohmann::json j;
    j["model"]      = model;
    j["max_tokens"] = max_tokens;
    j["stream"]     = stream;

    if (!system_prompt.empty()) {
        j["system"] = system_prompt;
    }

    j["messages"] = nlohmann::json::array();
    for (const auto& msg : messages) {
        nlohmann::json mj;
        clawed::api::to_json(mj, msg);
        j["messages"].push_back(std::move(mj));
    }

    if (!tools.empty()) {
        j["tools"] = nlohmann::json::array();
        for (const auto& tool : tools) {
            j["tools"].push_back(tool.to_json());
        }
    }

    return j;
}

} // namespace clawed::api
