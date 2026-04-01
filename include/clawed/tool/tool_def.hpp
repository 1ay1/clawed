#pragma once

// ── ToolDef — zero-boilerplate tool definition ───────────────────────────────
//
// CRTP base that auto-derives name(), description(), input_schema(), and
// execute() from a simple struct declaration. Tool authors only write:
//
//   1. tool_name, tool_desc   (what the tool is)
//   2. params                 (what it takes — the schema IS the type)
//   3. run()                  (what it does — receives typed, validated args)
//
// Example — complete tool definition in 12 lines:
//
//   struct Bash : ToolDef<Bash> {
//       static constexpr std::string_view tool_name = "bash";
//       static constexpr std::string_view tool_desc = "Execute a bash command";
//       static constexpr auto params = std::tuple{
//           Required<std::string>{"command", "The bash command to execute"},
//           Optional<int>{"timeout", "Timeout in seconds", 120},
//       };
//       static auto run(AnyExecutor& exec, std::string command, int timeout)
//           -> Result<std::string>
//       {
//           return exec.run_command(command, std::chrono::seconds{timeout});
//       }
//   };
//
// The framework generates:
//   - input_schema() → {"type":"object","required":["command"],"properties":{...}}
//   - execute(json, exec) → extracts params, validates, calls run()
//   - name() / description() → from constexpr strings
//
// Schema generation and parameter extraction are compile-time resolved.
// No virtual dispatch. No JSON boilerplate in tool logic.

#include <clawed/tool/field.hpp>
#include <clawed/executor/concept.hpp>

namespace clawed::tools {

template <typename Derived>
struct ToolDef {
    static constexpr auto name() -> std::string_view {
        return Derived::tool_name;
    }

    static constexpr auto description() -> std::string_view {
        return Derived::tool_desc;
    }

    static auto input_schema() -> nlohmann::json {
        return generate_schema(Derived::params);
    }

    auto execute(const nlohmann::json& input, AnyExecutor& exec) const
        -> Result<std::string>
    {
        auto params = extract_params(input, Derived::params);
        if (!params) return std::unexpected(params.error());

        return std::apply([&](auto&&... args) {
            return Derived::run(exec, std::forward<decltype(args)>(args)...);
        }, std::move(*params));
    }
};

} // namespace clawed::tools
