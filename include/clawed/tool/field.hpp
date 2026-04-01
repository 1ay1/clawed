#pragma once

// ── Declarative tool parameters ──────────────────────────────────────────────
//
// Parameters are compile-time descriptors. The framework derives:
//   - JSON input_schema (no hand-written JSON)
//   - Extraction from input JSON (no manual json["key"].get<T>())
//   - Validation (required checks, type checks)
//   - Error messages (from descriptions)
//
// Usage:
//   static constexpr auto params = std::tuple{
//       Required<std::string>{"command", "The bash command to execute"},
//       Optional<int>{"timeout", "Timeout in seconds", 120},
//   };
//
// The type system IS the schema. Required<std::string> maps to
// {"type": "string"} with the field in "required". Optional<int>
// maps to {"type": "integer"} without.

#include <clawed/core/types.hpp>
#include <clawed/core/error.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <tuple>

namespace clawed::tools {

// ── Parameter descriptors ────────────────────────────────────────────────────

template <typename T>
struct Required {
    const char* name;
    const char* desc;
    using type = T;
    static constexpr bool is_required = true;
};

template <typename T>
struct Optional {
    const char* name;
    const char* desc;
    T default_value;
    using type = T;
    static constexpr bool is_required = false;
};

// ── JSON type mapping ────────────────────────────────────────────────────────

template <typename T> constexpr const char* json_type_name = "string";
template <> inline constexpr const char* json_type_name<int> = "integer";
template <> inline constexpr const char* json_type_name<bool> = "boolean";
template <> inline constexpr const char* json_type_name<double> = "number";

// ── Schema generation ────────────────────────────────────────────────────────
// Generates JSON input_schema from a tuple of field descriptors.

namespace detail {

template <typename Field>
void add_property(nlohmann::json& props, nlohmann::json& req, const Field& f) {
    props[f.name] = {
        {"type", json_type_name<typename Field::type>},
        {"description", f.desc}
    };
    if constexpr (Field::is_required) {
        req.push_back(f.name);
    }
}

} // namespace detail

template <typename... Fields>
auto generate_schema(const std::tuple<Fields...>& fields) -> nlohmann::json {
    nlohmann::json schema = {{"type", "object"}};
    nlohmann::json props = nlohmann::json::object();
    nlohmann::json req = nlohmann::json::array();

    std::apply([&](const auto&... f) {
        (detail::add_property(props, req, f), ...);
    }, fields);

    schema["properties"] = std::move(props);
    if (!req.empty()) schema["required"] = std::move(req);
    return schema;
}

// ── Parameter extraction ─────────────────────────────────────────────────────
// Extracts typed values from JSON input using field descriptors.
// Returns Result<tuple<...>> — either all params or first error.

namespace detail {

template <typename Field>
auto extract_one(const nlohmann::json& input, const Field& f)
    -> Result<typename Field::type>
{
    using T = typename Field::type;

    if (!input.contains(f.name)) {
        if constexpr (Field::is_required) {
            return make_error(ErrorCode::ToolExecFailed,
                std::format("missing required parameter '{}'", f.name));
        } else {
            return f.default_value;
        }
    }

    const auto& val = input[f.name];

    // Type checking
    if constexpr (std::same_as<T, std::string>) {
        if (!val.is_string())
            return make_error(ErrorCode::ToolExecFailed,
                std::format("'{}' must be a string", f.name));
        return val.template get<std::string>();
    } else if constexpr (std::same_as<T, int>) {
        if (!val.is_number_integer())
            return make_error(ErrorCode::ToolExecFailed,
                std::format("'{}' must be an integer", f.name));
        return val.template get<int>();
    } else if constexpr (std::same_as<T, bool>) {
        if (!val.is_boolean())
            return make_error(ErrorCode::ToolExecFailed,
                std::format("'{}' must be a boolean", f.name));
        return val.template get<bool>();
    } else if constexpr (std::same_as<T, double>) {
        if (!val.is_number())
            return make_error(ErrorCode::ToolExecFailed,
                std::format("'{}' must be a number", f.name));
        return val.template get<double>();
    } else {
        return val.template get<T>();
    }
}

// Extract all fields into a tuple, short-circuiting on first error.
template <typename... Fields, size_t... Is>
auto extract_all_impl(const nlohmann::json& input,
                      const std::tuple<Fields...>& fields,
                      std::index_sequence<Is...>)
    -> Result<std::tuple<typename Fields::type...>>
{
    // We need to extract one at a time and check for errors.
    // Use a fold-friendly approach with an error accumulator.
    std::optional<Error> first_error;

    auto try_extract = [&]<typename F>(const F& field)
        -> typename F::type
    {
        if (first_error) return typename F::type{};
        auto result = extract_one(input, field);
        if (!result) {
            first_error = result.error();
            return typename F::type{};
        }
        return std::move(*result);
    };

    auto values = std::tuple{try_extract(std::get<Is>(fields))...};

    if (first_error)
        return std::unexpected(std::move(*first_error));

    return values;
}

} // namespace detail

template <typename... Fields>
auto extract_params(const nlohmann::json& input,
                    const std::tuple<Fields...>& fields)
    -> Result<std::tuple<typename Fields::type...>>
{
    return detail::extract_all_impl(input, fields,
        std::index_sequence_for<Fields...>{});
}

} // namespace clawed::tools
