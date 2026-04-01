#pragma once

// ── Tool concept & type erasure ──────────────────────────────────────────────
//
// A Tool is any type that can:
//   - Declare its name, description, and input schema
//   - Execute given JSON input and an executor
//
// Tools are stateless — they receive the executor at call time,
// not at construction. This means:
//   - One tool instance works with any executor (local, SSH, mock)
//   - Tools can be constexpr-constructible
//   - No shared_ptr, no ref counting, no coupling
//
// AnyTool type-erases any Tool for storage in the registry.

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <clawed/executor/concept.hpp>
#include <clawed/api/messages.hpp>
#include <nlohmann/json.hpp>

#include <concepts>
#include <memory>
#include <string>
#include <string_view>

namespace clawed {

// ── Tool concept ─────────────────────────────────────────────────────────────

template <typename T>
concept Tool = requires(const T t, const nlohmann::json& input, AnyExecutor& exec) {
    { T::name() }         -> std::convertible_to<std::string_view>;
    { T::description() }  -> std::convertible_to<std::string_view>;
    { T::input_schema() } -> std::convertible_to<nlohmann::json>;
    { t.execute(input, exec) } -> std::same_as<Result<std::string>>;
};

// ── Type-erased tool wrapper ─────────────────────────────────────────────────

class AnyTool {
public:
    template <Tool T>
    explicit AnyTool(T tool)
        : impl_(std::make_unique<Model<T>>(std::move(tool))) {}

    AnyTool(AnyTool&&) noexcept = default;
    AnyTool& operator=(AnyTool&&) noexcept = default;

    [[nodiscard]] auto name() const -> std::string_view { return impl_->name(); }
    [[nodiscard]] auto description() const -> std::string_view { return impl_->description(); }
    [[nodiscard]] auto input_schema() const -> nlohmann::json { return impl_->input_schema(); }

    [[nodiscard]] auto execute(const nlohmann::json& input, AnyExecutor& exec) const
        -> Result<std::string>
    {
        return impl_->execute(input, exec);
    }

    [[nodiscard]] auto to_definition() const -> api::ToolDefinition {
        return {
            .name         = std::string(name()),
            .description  = std::string(description()),
            .input_schema = input_schema()
        };
    }

private:
    struct Concept {
        virtual ~Concept() = default;
        [[nodiscard]] virtual auto name() const -> std::string_view = 0;
        [[nodiscard]] virtual auto description() const -> std::string_view = 0;
        [[nodiscard]] virtual auto input_schema() const -> nlohmann::json = 0;
        [[nodiscard]] virtual auto execute(const nlohmann::json&, AnyExecutor&) const
            -> Result<std::string> = 0;
    };

    template <Tool T>
    struct Model final : Concept {
        T tool;
        explicit Model(T t) : tool(std::move(t)) {}
        auto name() const -> std::string_view override { return T::name(); }
        auto description() const -> std::string_view override { return T::description(); }
        auto input_schema() const -> nlohmann::json override { return T::input_schema(); }
        auto execute(const nlohmann::json& input, AnyExecutor& exec) const
            -> Result<std::string> override { return tool.execute(input, exec); }
    };

    std::unique_ptr<Concept> impl_;
};

} // namespace clawed
