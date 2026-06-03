#include <merak/fs_tools.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <future>

namespace fs = std::filesystem;

namespace merak::tools {

// ========== ReadFileTool ==========

ToolSpec ReadFileTool::spec() const {
    ToolSpec s;
    s.name = "read_file";
    s.description = "Read the contents of a file at the given path";
    s.source = "builtin";
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to the file to read"}
        },
        "required": ["path"]
    })JSON";
    return s;
}

std::future<ToolResult> ReadFileTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args["path"].get<std::string>();

            if (!fs::exists(path)) {
                result.is_error = true;
                result.output = "File not found: " + path;
                return result;
            }

            if (!fs::is_regular_file(path)) {
                result.is_error = true;
                result.output = "Not a regular file: " + path;
                return result;
            }

            std::ifstream f(path);
            if (!f.is_open()) {
                result.is_error = true;
                result.output = "Cannot open file: " + path;
                return result;
            }

            std::ostringstream oss;
            oss << f.rdbuf();
            result.output = oss.str();
            spdlog::debug("ReadFile: read {} bytes from {}", result.output.size(), path);
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("ReadFile error: ") + e.what();
        }

        return result;
    });
}

// ========== WriteFileTool ==========

ToolSpec WriteFileTool::spec() const {
    ToolSpec s;
    s.name = "write_file";
    s.description = "Write content to a file, creating it if it doesn't exist";
    s.source = "builtin";
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to write to"},
            "content": {"type": "string", "description": "Content to write"}
        },
        "required": ["path", "content"]
    })JSON";
    return s;
}

std::future<ToolResult> WriteFileTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args["path"].get<std::string>();
            std::string content = args["content"].get<std::string>();

            fs::create_directories(fs::path(path).parent_path());

            std::ofstream f(path);
            if (!f.is_open()) {
                result.is_error = true;
                result.output = "Cannot write to file: " + path;
                return result;
            }

            f << content;
            f.close();
            result.output = "File written: " + path + " (" +
                std::to_string(content.size()) + " bytes)";
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("WriteFile error: ") + e.what();
        }

        return result;
    });
}

// ========== StrReplaceTool ==========

ToolSpec StrReplaceTool::spec() const {
    ToolSpec s;
    s.name = "str_replace";
    s.description = "Replace a string in a file. old_str must match exactly once";
    s.source = "builtin";
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "File to edit"},
            "old_str": {"type": "string", "description": "Text to find and replace"},
            "new_str": {"type": "string", "description": "Replacement text"}
        },
        "required": ["path", "old_str", "new_str"]
    })JSON";
    return s;
}

std::future<ToolResult> StrReplaceTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call), journal = journal_]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args["path"].get<std::string>();
            std::string old_str = args["old_str"].get<std::string>();
            std::string new_str = args["new_str"].get<std::string>();

            std::ifstream f_in(path);
            if (!f_in.is_open()) {
                result.is_error = true;
                result.output = "Cannot open file: " + path;
                return result;
            }
            std::ostringstream oss;
            oss << f_in.rdbuf();
            f_in.close();
            std::string content = oss.str();

            size_t pos = content.find(old_str);
            if (pos == std::string::npos) {
                result.is_error = true;
                result.output = "old_str not found in file. Ensure exact whitespace match.";
                return result;
            }

            size_t pos2 = content.find(old_str, pos + 1);
            if (pos2 != std::string::npos) {
                result.is_error = true;
                result.output = "old_str matches multiple locations. "
                    "Provide a larger string with more surrounding context.";
                return result;
            }

            // Record before state to EditJournal
            if (journal) {
                journal->record(fs::path(path), content, "");
            }

            content.replace(pos, old_str.size(), new_str);

            // Record after state
            if (journal) {
                // Update the last entry with the after state
                auto& entries = const_cast<std::vector<EditJournal::Entry>&>(journal->entries());
                if (!entries.empty()) {
                    entries.back().after = content;
                }
            }

            std::ofstream f_out(path);
            f_out << content;
            f_out.close();

            result.output = "Edit applied to " + path;
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("StrReplace error: ") + e.what();
        }

        return result;
    });
}

// ========== MultiEditTool ==========

ToolSpec MultiEditTool::spec() const {
    ToolSpec s;
    s.name = "multi_edit";
    s.description = "Apply multiple string replacements atomically. "
        "Each edit's old_str must match exactly once and edits must not overlap.";
    s.source = "builtin";
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "File to edit"},
            "edits": {
                "type": "array",
                "description": "List of edits to apply",
                "items": {
                    "type": "object",
                    "properties": {
                        "old_str": {"type": "string", "description": "Text to find"},
                        "new_str": {"type": "string", "description": "Replacement text"}
                    },
                    "required": ["old_str", "new_str"]
                }
            }
        },
        "required": ["path", "edits"]
    })JSON";
    return s;
}

std::future<ToolResult> MultiEditTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call), journal = journal_]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args["path"].get<std::string>();
            auto edits = args["edits"];

            if (!edits.is_array() || edits.empty()) {
                result.is_error = true;
                result.output = "edits must be a non-empty array of {old_str, new_str} objects";
                return result;
            }

            // Read file
            std::ifstream f_in(path);
            if (!f_in.is_open()) {
                result.is_error = true;
                result.output = "Cannot open file: " + path;
                return result;
            }
            std::ostringstream oss;
            oss << f_in.rdbuf();
            f_in.close();
            std::string content = oss.str();

            // Record before state
            if (journal) {
                journal->record(fs::path(path), content, "");
            }

            // First pass: verify all edits apply cleanly (unique match) and no overlaps
            struct EditMatch {
                std::string old_str;
                std::string new_str;
                size_t pos;
            };
            std::vector<EditMatch> matches;
            matches.reserve(edits.size());

            for (size_t i = 0; i < edits.size(); ++i) {
                std::string old_str = edits[i]["old_str"].get<std::string>();
                std::string new_str = edits[i]["new_str"].get<std::string>();

                size_t pos = content.find(old_str);
                if (pos == std::string::npos) {
                    result.is_error = true;
                    result.output = "Edit " + std::to_string(i) +
                        ": old_str not found in file. Ensure exact whitespace match.";
                    return result;
                }

                size_t pos2 = content.find(old_str, pos + 1);
                if (pos2 != std::string::npos) {
                    result.is_error = true;
                    result.output = "Edit " + std::to_string(i) +
                        ": old_str matches multiple locations. "
                        "Provide a larger string with more surrounding context.";
                    return result;
                }

                matches.push_back({std::move(old_str), std::move(new_str), pos});
            }

            // Check for overlapping edits
            for (size_t i = 0; i < matches.size(); ++i) {
                for (size_t j = i + 1; j < matches.size(); ++j) {
                    size_t start_i = matches[i].pos;
                    size_t end_i = start_i + matches[i].old_str.size();
                    size_t start_j = matches[j].pos;
                    size_t end_j = start_j + matches[j].old_str.size();

                    if (start_i < end_j && start_j < end_i) {
                        result.is_error = true;
                        result.output = "Overlapping edits detected: edit " +
                            std::to_string(i) + " and edit " + std::to_string(j);
                        return result;
                    }
                }
            }

            // Sort by position descending (reverse order) to preserve positions
            std::sort(matches.begin(), matches.end(),
                [](const EditMatch& a, const EditMatch& b) {
                    return a.pos > b.pos;
                });

            // Apply edits in reverse order
            for (const auto& m : matches) {
                content.replace(m.pos, m.old_str.size(), m.new_str);
            }

            // Record after state
            if (journal) {
                auto& entries = const_cast<std::vector<EditJournal::Entry>&>(journal->entries());
                if (!entries.empty()) {
                    entries.back().after = content;
                }
            }

            // Write back
            std::ofstream f_out(path);
            f_out << content;
            f_out.close();

            result.output = "Applied " + std::to_string(matches.size()) +
                " edits to " + path;
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("MultiEdit error: ") + e.what();
        }

        return result;
    });
}

// ========== DeleteFileTool ==========

ToolSpec DeleteFileTool::spec() const {
    ToolSpec s;
    s.name = "delete_file";
    s.description = "Delete a file. Refuses to delete directories or .git contents.";
    s.source = "builtin";
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to the file to delete"},
            "explanation": {"type": "string", "description": "Reason for deleting the file"}
        },
        "required": ["path", "explanation"]
    })JSON";
    return s;
}

std::future<ToolResult> DeleteFileTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call), journal = journal_]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path_str = args["path"].get<std::string>();
            // explanation is required but we just log it
            std::string explanation = args["explanation"].get<std::string>();
            spdlog::info("DeleteFile: {} — reason: {}", path_str, explanation);

            fs::path p(path_str);

            // Resolve to canonical path for safety checks
            std::string canonical;
            try {
                canonical = fs::canonical(fs::absolute(p)).string();
            } catch (const fs::filesystem_error&) {
                // File may not exist yet, use absolute path
                canonical = fs::absolute(p).string();
            }

            // Refuse to delete .git/ contents
            if (canonical.find("/.git/") != std::string::npos ||
                canonical.find("/.git") == canonical.size() - 5) {
                result.is_error = true;
                result.output = "Refusing to delete .git contents: " + path_str;
                return result;
            }

            // Check if file exists
            if (!fs::exists(p)) {
                result.is_error = true;
                result.output = "File does not exist: " + path_str;
                return result;
            }

            // Refuse to delete directories
            if (fs::is_directory(p)) {
                result.is_error = true;
                result.output = "Refusing to delete directory. Use a shell command to remove directories: " + path_str;
                return result;
            }

            if (!fs::is_regular_file(p) && !fs::is_symlink(p)) {
                result.is_error = true;
                result.output = "Not a regular file: " + path_str;
                return result;
            }

            // Record before state to EditJournal
            if (journal) {
                std::string before;
                if (fs::exists(p)) {
                    std::ifstream f(p);
                    if (f.is_open()) {
                        std::ostringstream oss;
                        oss << f.rdbuf();
                        before = oss.str();
                    }
                }
                journal->record(p, before, "");
            }

            // Delete the file
            fs::remove(p);

            result.output = "File deleted: " + path_str;
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("DeleteFile error: ") + e.what();
        }

        return result;
    });
}

// ========== ListDirTool ==========

ToolSpec ListDirTool::spec() const {
    ToolSpec s;
    s.name = "list_dir";
    s.description = "List contents of a directory recursively with depth control. "
        "Skips common ignored directories (node_modules, .git, etc.)";
    s.source = "builtin";
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "path": {
                "type": "string",
                "description": "Directory path to list (default: current directory)"
            },
            "depth": {
                "type": "integer",
                "description": "Maximum recursion depth (default: 1, max: 10)"
            },
            "show_hidden": {
                "type": "boolean",
                "description": "Include hidden files and directories (default: false)"
            }
        },
        "required": []
    })JSON";
    return s;
}

// Directories to skip during listing
static const std::vector<std::string> kSkipDirs = {
    "node_modules", "target", "__pycache__", ".git",
    "build", ".venv", "venv"
};

static bool should_skip_dir(const std::string& name) {
    for (const auto& skip : kSkipDirs) {
        if (name == skip) return true;
    }
    return false;
}

static void list_dir_recursive(
    const fs::path& dir,
    const fs::path& base_path,
    int depth,
    int max_depth,
    bool show_hidden,
    nlohmann::json& entries,
    int& count,
    int max_entries)
{
    if (depth > max_depth || count >= max_entries) return;

    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (count >= max_entries) break;

        std::string name = entry.path().filename().string();

        // Skip hidden files/dirs unless requested
        if (!show_hidden && !name.empty() && name[0] == '.') {
            continue;
        }

        nlohmann::json item;
        item["name"] = fs::relative(entry.path(), base_path).string();

        if (entry.is_directory()) {
            if (should_skip_dir(name)) continue;
            item["type"] = "directory";
            item["size"] = nullptr;
        } else if (entry.is_regular_file()) {
            item["type"] = "file";
            item["size"] = entry.file_size();
        } else if (entry.is_symlink()) {
            item["type"] = "symlink";
            item["size"] = nullptr;
        } else {
            item["type"] = "other";
            item["size"] = nullptr;
        }

        entries.push_back(item);
        count++;

        // Recurse into directories
        if (entry.is_directory() && !should_skip_dir(name)) {
            list_dir_recursive(entry.path(), base_path, depth + 1, max_depth,
                              show_hidden, entries, count, max_entries);
        }
    }
}

std::future<ToolResult> ListDirTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path_str = args.value("path", ".");
            int depth = args.value("depth", 1);
            bool show_hidden = args.value("show_hidden", false);

            // Clamp depth
            if (depth < 1) depth = 1;
            if (depth > 10) depth = 10;

            fs::path p(path_str);
            if (!fs::exists(p)) {
                result.is_error = true;
                result.output = "Directory not found: " + path_str;
                return result;
            }

            if (!fs::is_directory(p)) {
                result.is_error = true;
                result.output = "Not a directory: " + path_str;
                return result;
            }

            nlohmann::json entries = nlohmann::json::array();
            int count = 0;
            constexpr int kMaxEntries = 500;

            list_dir_recursive(p, p, 1, depth, show_hidden, entries, count, kMaxEntries);

            result.output = entries.dump(2);
            if (count >= kMaxEntries) {
                result.output += "\n[Truncated at " + std::to_string(kMaxEntries) + " entries]";
            }
            result.truncated = (count >= kMaxEntries);
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("ListDir error: ") + e.what();
        }

        return result;
    });
}

} // namespace merak::tools
