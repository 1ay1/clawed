#include <clawed/tool/builtin/write_file.hpp>
#include <filesystem>
#include <fstream>
#include <format>

namespace clawed::builtin {

auto WriteFileTool::execute(const nlohmann::json& input) const -> Result<std::string> {
    if (!input.contains("file_path") || !input["file_path"].is_string()) {
        return make_error(ErrorCode::ToolExecFailed, "missing 'file_path' parameter");
    }
    if (!input.contains("content") || !input["content"].is_string()) {
        return make_error(ErrorCode::ToolExecFailed, "missing 'content' parameter");
    }

    auto path    = input["file_path"].get<std::string>();
    auto content = input["content"].get<std::string>();

    // Ensure parent directory exists.
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return make_error(ErrorCode::ToolExecFailed,
            std::format("cannot write to: {}", path));
    }

    file << content;
    file.close();

    auto size = std::filesystem::file_size(path);
    return std::format("Wrote {} bytes to {}", size, path);
}

} // namespace clawed::builtin
