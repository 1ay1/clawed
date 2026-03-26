#include <clawed/tool/builtin/glob.hpp>
#include <filesystem>
#include <format>
#include <fnmatch.h>
#include <algorithm>
#include <vector>

namespace clawed::builtin {

namespace {

// Recursive glob match using fnmatch.
auto glob_match(const std::filesystem::path& root, const std::string& pattern)
    -> std::vector<std::string>
{
    std::vector<std::string> results;

    // Handle ** patterns by walking the full tree.
    bool recursive = pattern.find("**") != std::string::npos;

    auto iterator_options = std::filesystem::directory_options::skip_permission_denied;

    if (recursive) {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(root, iterator_options))
        {
            auto rel = std::filesystem::relative(entry.path(), root).string();
            if (fnmatch(pattern.c_str(), rel.c_str(), FNM_PATHNAME) == 0) {
                results.push_back(entry.path().string());
            }
        }
    } else {
        for (const auto& entry :
             std::filesystem::directory_iterator(root, iterator_options))
        {
            auto name = entry.path().filename().string();
            if (fnmatch(pattern.c_str(), name.c_str(), 0) == 0) {
                results.push_back(entry.path().string());
            }
        }
    }

    std::sort(results.begin(), results.end());
    return results;
}

} // anonymous namespace

auto GlobTool::execute(const nlohmann::json& input) const -> Result<std::string> {
    if (!input.contains("pattern") || !input["pattern"].is_string()) {
        return make_error(ErrorCode::ToolExecFailed, "missing 'pattern' parameter");
    }

    auto pattern = input["pattern"].get<std::string>();

    std::filesystem::path root = std::filesystem::current_path();
    if (input.contains("path") && input["path"].is_string()) {
        root = input["path"].get<std::string>();
    }

    if (!std::filesystem::exists(root)) {
        return make_error(ErrorCode::ToolExecFailed,
            std::format("directory not found: {}", root.string()));
    }

    auto matches = glob_match(root, pattern);

    if (matches.empty()) {
        return std::string("No matches found.");
    }

    std::string result;
    size_t limit = std::min(matches.size(), size_t{500});
    for (size_t i = 0; i < limit; ++i) {
        result += matches[i];
        result += '\n';
    }
    if (matches.size() > 500) {
        result += std::format("... and {} more\n", matches.size() - 500);
    }

    return result;
}

} // namespace clawed::builtin
