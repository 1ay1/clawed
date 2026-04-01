#pragma once

#include <clawed/tool/concept.hpp>
#include <clawed/api/messages.hpp>

#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace clawed {

struct StringHash {
    using is_transparent = void;
    auto operator()(std::string_view sv) const noexcept -> size_t {
        return std::hash<std::string_view>{}(sv);
    }
};

class ToolRegistry {
public:
    explicit ToolRegistry(std::shared_ptr<AnyExecutor> executor)
        : executor_(std::move(executor)) {}

    template <Tool T>
    void register_tool(T tool) {
        auto n = std::string(T::name());
        tools_.emplace(std::move(n), AnyTool(std::move(tool)));
    }

    [[nodiscard]] auto find(std::string_view name) const -> const AnyTool* {
        auto it = tools_.find(name);
        return (it != tools_.end()) ? &it->second : nullptr;
    }

    auto execute(std::string_view name, const nlohmann::json& input) const
        -> Result<std::string>
    {
        if (auto* tool = find(name))
            return tool->execute(input, *executor_);
        return make_error(ErrorCode::ToolNotFound,
                          std::format("tool '{}' not found", name));
    }

    [[nodiscard]] auto definitions() const -> std::vector<api::ToolDefinition> {
        std::vector<api::ToolDefinition> defs;
        defs.reserve(tools_.size());
        for (const auto& [_, tool] : tools_)
            defs.push_back(tool.to_definition());
        return defs;
    }

    [[nodiscard]] auto size() const noexcept -> size_t { return tools_.size(); }

private:
    std::shared_ptr<AnyExecutor> executor_;
    std::unordered_map<std::string, AnyTool, StringHash, std::equal_to<>>
        tools_;
};

} // namespace clawed
