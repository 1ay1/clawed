#include <clawed/tool/builtin/read_file.hpp>
#include <filesystem>
#include <fstream>
#include <format>
#include <sstream>

namespace clawed::builtin {

auto ReadFileTool::execute(const nlohmann::json& input) const -> Result<std::string> {
    if (!input.contains("file_path") || !input["file_path"].is_string()) {
        return make_error(ErrorCode::ToolExecFailed, "missing 'file_path' parameter");
    }

    auto path = input["file_path"].get<std::string>();

    if (!std::filesystem::exists(path)) {
        return make_error(ErrorCode::ToolExecFailed,
            std::format("file not found: {}", path));
    }

    if (!std::filesystem::is_regular_file(path)) {
        return make_error(ErrorCode::ToolExecFailed,
            std::format("not a regular file: {}", path));
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return make_error(ErrorCode::ToolExecFailed,
            std::format("cannot open file: {}", path));
    }

    int offset = 1;
    int limit  = 2000;
    if (input.contains("offset") && input["offset"].is_number_integer()) {
        offset = std::max(1, input["offset"].get<int>());
    }
    if (input.contains("limit") && input["limit"].is_number_integer()) {
        limit = input["limit"].get<int>();
    }

    std::string result;
    std::string line;
    int line_num = 0;
    int lines_read = 0;

    while (std::getline(file, line)) {
        ++line_num;
        if (line_num < offset) continue;
        if (lines_read >= limit) break;

        result += std::format("{:6d}\t{}\n", line_num, line);
        ++lines_read;
    }

    if (result.empty()) {
        return std::string("(empty file or offset beyond file end)");
    }

    return result;
}

} // namespace clawed::builtin
