#include <merak/search_tools.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <future>
#include <regex>
#include <glob.h>
#include <fnmatch.h>

namespace fs = std::filesystem;

namespace merak::tools {

// ========== GlobTool ==========

ToolSpec GlobTool::spec() const {
    ToolSpec s;
    s.name = "glob";
    s.description = "Find files matching a glob pattern using system glob(3)";
    s.source = "builtin";
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "pattern": {
                "type": "string",
                "description": "Glob pattern to match (supports POSIX glob syntax, e.g. **/*.cpp)"
            }
        },
        "required": ["pattern"]
    })JSON";
    return s;
}

std::future<ToolResult> GlobTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string pattern = args["pattern"].get<std::string>();

            glob_t glob_result;
            int ret = glob(pattern.c_str(), GLOB_TILDE, nullptr, &glob_result);

            if (ret == GLOB_NOMATCH) {
                globfree(&glob_result);
                result.output = "No files matched pattern: " + pattern;
                return result;
            }

            if (ret != 0) {
                globfree(&glob_result);
                result.is_error = true;
                result.output = "Glob error for pattern: " + pattern;
                return result;
            }

            // Collect, sort (glob returns sorted by default), and dedup
            std::vector<std::string> matches;
            constexpr size_t kMaxMatches = 100;

            for (size_t i = 0; i < glob_result.gl_pathc && matches.size() < kMaxMatches; ++i) {
                std::string path(glob_result.gl_pathv[i]);
                // Dedup: skip consecutive duplicates (glob results are sorted)
                if (matches.empty() || matches.back() != path) {
                    matches.push_back(std::move(path));
                }
            }

            globfree(&glob_result);

            // Build output with 100KB cap
            std::ostringstream output;
            constexpr size_t kMaxOutput = 100 * 1024;
            bool truncated = false;

            for (const auto& m : matches) {
                std::string line = m + "\n";
                if (output.str().size() + line.size() > kMaxOutput) {
                    truncated = true;
                    break;
                }
                output << line;
            }

            result.output = output.str();
            result.truncated = truncated;

            if (result.output.empty()) {
                result.output = "No files matched";
            }
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("Glob error: ") + e.what();
        }

        return result;
    });
}

// ========== GrepTool ==========

ToolSpec GrepTool::spec() const {
    ToolSpec s;
    s.name = "grep";
    s.description = "Search for a regex pattern in text files";
    s.source = "builtin";
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "pattern": {
                "type": "string",
                "description": "Regex pattern to search for (ECMAScript syntax)"
            },
            "path": {
                "type": "string",
                "description": "File or directory to search in (default: current directory)"
            },
            "include": {
                "type": "string",
                "description": "Glob pattern to filter which files to search"
            },
            "exclude": {
                "type": "string",
                "description": "Glob pattern to filter which files to skip"
            }
        },
        "required": ["pattern"]
    })JSON";
    return s;
}

std::future<ToolResult> GrepTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string pattern_str = args["pattern"].get<std::string>();
            std::string path_str = args.value("path", ".");
            std::string include_pat = args.value("include", "");
            std::string exclude_pat = args.value("exclude", "");

            std::regex re(pattern_str, std::regex::ECMAScript);
            std::ostringstream output;
            size_t output_size = 0;
            constexpr size_t kMaxOutput = 10 * 1024; // 10KB
            bool truncated = false;

            // Check if a file is binary by looking for NUL bytes in the first 1024 bytes
            auto is_binary = [](const std::string& filepath) -> bool {
                std::ifstream f(filepath, std::ios::binary);
                if (!f.is_open()) return true;
                char buf[1024];
                f.read(buf, sizeof(buf));
                size_t n = f.gcount();
                for (size_t i = 0; i < n; ++i) {
                    if (buf[i] == '\0') return true;
                }
                return false;
            };

            // Check if a relative path passes the include/exclude glob filters
            auto matches_filter = [&](const std::string& rel_path) -> bool {
                if (!include_pat.empty()) {
                    if (fnmatch(include_pat.c_str(), rel_path.c_str(), 0) != 0)
                        return false;
                }
                if (!exclude_pat.empty()) {
                    if (fnmatch(exclude_pat.c_str(), rel_path.c_str(), 0) == 0)
                        return false;
                }
                return true;
            };

            // Search a single text file for regex matches
            auto search_file = [&](const fs::path& filepath, const std::string& rel_path) {
                if (truncated) return;
                if (is_binary(filepath.string())) return;

                std::ifstream f(filepath);
                if (!f.is_open()) return;

                std::string line;
                int lineno = 0;
                while (std::getline(f, line) && !truncated) {
                    lineno++;
                    if (std::regex_search(line, re)) {
                        std::string match_line = rel_path + ":" +
                            std::to_string(lineno) + ":" + line + "\n";
                        if (output_size + match_line.size() > kMaxOutput) {
                            truncated = true;
                            return;
                        }
                        output << match_line;
                        output_size += match_line.size();
                    }
                }
            };

            fs::path root(path_str);
            if (fs::is_regular_file(root)) {
                std::string rel_path = root.string();
                if (matches_filter(rel_path)) {
                    search_file(root, rel_path);
                }
            } else if (fs::is_directory(root)) {
                for (auto& entry : fs::recursive_directory_iterator(root)) {
                    if (truncated) break;
                    if (!entry.is_regular_file()) continue;
                    std::string rel_path = fs::relative(entry.path(), root).string();
                    if (!matches_filter(rel_path)) continue;
                    search_file(entry.path(), rel_path);
                }
            } else {
                result.is_error = true;
                result.output = "Path not found or not a regular file/directory: " + path_str;
                return result;
            }

            result.output = output.str();
            result.truncated = truncated;
            if (result.output.empty() && !truncated) {
                result.output = "No matches found";
            }
        } catch (const std::regex_error& e) {
            result.is_error = true;
            result.output = std::string("Regex error: ") + e.what();
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("Grep error: ") + e.what();
        }

        return result;
    });
}

} // namespace merak::tools
