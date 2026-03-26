#pragma once

#include <clawed/core/types.hpp>
#include <clawed/api/messages.hpp>
#include <clawed/tool/registry.hpp>
#include <string>
#include <vector>

namespace clawed {

class Conversation {
public:
    void set_system_prompt(std::string prompt) {
        system_prompt_ = std::move(prompt);
    }

    void add_user_message(std::string text) {
        api::Message msg;
        msg.role = Role::User;
        msg.content.emplace_back(api::TextContent{std::move(text)});
        messages_.push_back(std::move(msg));
    }

    void add_assistant_text(std::string text) {
        api::Message msg;
        msg.role = Role::Assistant;
        msg.content.emplace_back(api::TextContent{std::move(text)});
        messages_.push_back(std::move(msg));
    }

    void add_assistant_response(std::vector<api::ContentBlock> blocks) {
        api::Message msg;
        msg.role    = Role::Assistant;
        msg.content = std::move(blocks);
        messages_.push_back(std::move(msg));
    }

    void add_tool_results(
        const std::vector<std::tuple<ToolUseId, std::string, bool>>& results)
    {
        api::Message msg;
        msg.role = Role::User;
        for (const auto& [id, output, is_error] : results) {
            msg.content.emplace_back(
                api::ToolResultContent{id, output, is_error});
        }
        messages_.push_back(std::move(msg));
    }

    [[nodiscard]] auto build_request(
        const ToolRegistry& registry,
        std::string_view model) const -> api::CreateMessageRequest
    {
        api::CreateMessageRequest req;
        req.model         = std::string(model);
        req.system_prompt = system_prompt_;
        req.messages      = messages_;
        req.tools         = registry.definitions();
        return req;
    }

    [[nodiscard]] auto message_count() const noexcept -> size_t {
        return messages_.size();
    }

    void clear() {
        messages_.clear();
    }

private:
    std::string                system_prompt_;
    std::vector<api::Message>  messages_;
};

} // namespace clawed
