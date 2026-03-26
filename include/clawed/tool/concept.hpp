#pragma once

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <clawed/api/messages.hpp>
#include <nlohmann/json.hpp>
#include <concepts>
#include <memory>
#include <string>
#include <string_view>

namespace clawed {

// ── Tool concept ────────────────────────────────────────────────────────────
// Any type satisfying this concept can be registered as a tool.
// No inheritance required. Just match the shape.

template <typename T>
concept Tool = requires(T t, const nlohmann::json& input) {
    { T::name() }         -> std::convertible_to<std::string_view>;
    { T::description() }  -> std::convertible_to<std::string_view>;
    { T::input_schema() } -> std::convertible_to<nlohmann::json>;
    { t.execute(input) }  -> std::same_as<Result<std::string>>;
};

// ── Type-erased tool wrapper (Sean Parent pattern) ──────────────────────────
// Stores any Tool in a uniform container without inheritance at the call site.

class AnyTool {
public:
    template <Tool T>
    explicit AnyTool(T tool)
        : impl_(std::make_unique<Model<T>>(std::move(tool))) {}

    AnyTool(AnyTool&&) noexcept = default;
    AnyTool& operator=(AnyTool&&) noexcept = default;

    [[nodiscard]] auto name() const -> std::string_view {
        return impl_->name();
    }

    [[nodiscard]] auto description() const -> std::string_view {
        return impl_->description();
    }

    [[nodiscard]] auto input_schema() const -> nlohmann::json {
        return impl_->input_schema();
    }

    [[nodiscard]] auto execute(const nlohmann::json& input) const
        -> Result<std::string>
    {
        return impl_->execute(input);
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
        [[nodiscard]] virtual auto execute(const nlohmann::json&) const
            -> Result<std::string> = 0;
    };

    template <Tool T>
    struct Model final : Concept {
        T tool;
        explicit Model(T t) : tool(std::move(t)) {}

        auto name() const -> std::string_view override { return T::name(); }
        auto description() const -> std::string_view override { return T::description(); }
        auto input_schema() const -> nlohmann::json override { return T::input_schema(); }
        auto execute(const nlohmann::json& input) const -> Result<std::string> override {
            return tool.execute(input);
        }
    };

    std::unique_ptr<Concept> impl_;
};

} // namespace clawed
