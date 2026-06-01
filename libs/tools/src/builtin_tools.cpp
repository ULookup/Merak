#include <merak/builtin_tools.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <cstdio>
#include <array>
#include <future>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>

namespace fs = std::filesystem;

namespace merak::tools {

// ========== ReadFile ==========

ToolSpec ReadFileTool::spec() const {
    ToolSpec s;
    s.name = "read_file";
    s.description = "Read the contents of a file at the given path";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to the file to read"}
        },
        "required": ["path"]
    })";
    return s;
}

std::future<ToolResult> ReadFileTool::execute(ToolCall call) {
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

// ========== WriteFile ==========

ToolSpec WriteFileTool::spec() const {
    ToolSpec s;
    s.name = "write_file";
    s.description = "Write content to a file, creating it if it doesn't exist";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to write to"},
            "content": {"type": "string", "description": "Content to write"}
        },
        "required": ["path", "content"]
    })";
    return s;
}

std::future<ToolResult> WriteFileTool::execute(ToolCall call) {
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

// ========== EditFile ==========

ToolSpec EditFileTool::spec() const {
    ToolSpec s;
    s.name = "edit_file";
    s.description = "Replace a string in a file. old_str must match exactly once";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "File to edit"},
            "old_str": {"type": "string", "description": "Text to find and replace"},
            "new_str": {"type": "string", "description": "Replacement text"}
        },
        "required": ["path", "old_str", "new_str"]
    })";
    return s;
}

std::future<ToolResult> EditFileTool::execute(ToolCall call) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
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

            content.replace(pos, old_str.size(), new_str);

            std::ofstream f_out(path);
            f_out << content;
            f_out.close();

            result.output = "Edit applied to " + path;
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("EditFile error: ") + e.what();
        }

        return result;
    });
}

// ========== Glob ==========

ToolSpec GlobTool::spec() const {
    ToolSpec s;
    s.name = "glob";
    s.description = "Find files matching a glob pattern (e.g. **/*.cpp)";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "pattern": {"type": "string", "description": "Glob pattern to match"}
        },
        "required": ["pattern"]
    })";
    return s;
}

static bool match_glob(const std::string& name, const std::string& pat) {
    size_t pi = 0, ni = 0;
    while (pi < pat.size() && ni < name.size()) {
        if (pat[pi] == '*') {
            pi++;
            if (pi == pat.size()) return true;
            while (ni < name.size() && name[ni] != pat[pi]) ni++;
        } else if (pat[pi] == '?' || pat[pi] == name[ni]) {
            pi++; ni++;
        } else {
            return false;
        }
    }
    return pi == pat.size() && ni == name.size();
}

std::future<ToolResult> GlobTool::execute(ToolCall call) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string pattern = args["pattern"].get<std::string>();

            std::vector<std::string> matches;
            bool recursive = (pattern.find("**") != std::string::npos);

            if (recursive) {
                for (auto& entry : fs::recursive_directory_iterator(".")) {
                    if (matches.size() >= 50) break;
                    std::string rel = fs::relative(entry.path(), ".").string();
                    if (match_glob(rel, pattern)) {
                        matches.push_back(rel);
                    }
                }
            } else {
                for (auto& entry : fs::directory_iterator(".")) {
                    if (matches.size() >= 50) break;
                    std::string name = entry.path().filename().string();
                    if (match_glob(name, pattern)) {
                        matches.push_back(name);
                    }
                }
            }

            if (matches.empty()) {
                result.output = "No files matched";
            } else {
                std::ostringstream oss;
                for (auto& m : matches) oss << m << "\n";
                result.output = oss.str();
            }
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("Glob error: ") + e.what();
        }

        return result;
    });
}

// ========== Grep ==========

ToolSpec GrepTool::spec() const {
    ToolSpec s;
    s.name = "grep";
    s.description = "Search for a regex pattern in files";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "pattern": {"type": "string", "description": "Regex pattern to search for"},
            "path": {"type": "string", "description": "File or directory to search in"}
        },
        "required": ["pattern"]
    })";
    return s;
}

std::future<ToolResult> GrepTool::execute(ToolCall call) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string pattern = args["pattern"].get<std::string>();
            std::string path_str = args.value("path", ".");

            std::regex re(pattern, std::regex::ECMAScript);
            std::ostringstream output;
            int match_count = 0;

            auto search_file = [&](const std::filesystem::path& p) {
                if (match_count >= 50) return;
                std::ifstream f(p);
                if (!f.is_open()) return;
                std::string line;
                int lineno = 0;
                while (std::getline(f, line) && match_count < 50) {
                    lineno++;
                    if (std::regex_search(line, re)) {
                        output << p.string() << ":" << lineno << ":" << line << "\n";
                        match_count++;
                    }
                }
            };

            fs::path root(path_str);
            if (fs::is_regular_file(root)) {
                search_file(root);
            } else if (fs::is_directory(root)) {
                for (auto& entry : fs::recursive_directory_iterator(root)) {
                    if (match_count >= 50) break;
                    if (!entry.is_regular_file()) continue;
                    std::string ext = entry.path().extension().string();
                    if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c") {
                        search_file(entry.path());
                    }
                }
            }

            result.output = output.str().empty() ? "No matches found" : output.str();
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

// ========== Bash ==========

bool BashTool::is_concurrent_safe(const ToolCall& call) const {
    try {
        auto args = nlohmann::json::parse(call.arguments);
        std::string command = args["command"].get<std::string>();

        static const std::vector<std::string> safe_prefixes = {
            "git status", "git log", "git diff", "git branch",
            "ls", "find", "grep", "cat", "head", "tail",
            "echo", "pwd", "which", "wc", "sort", "uniq"
        };
        for (auto& prefix : safe_prefixes) {
            if (command.rfind(prefix, 0) == 0) return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

ToolSpec BashTool::spec() const {
    ToolSpec s;
    s.name = "execute_bash";
    s.description = "Execute a bash command and return its output";
    s.source = "builtin";
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "command": {"type": "string", "description": "Bash command to execute"},
            "timeout_ms": {"type": "integer", "description": "Max time before kill (default 30000)"}
        },
        "required": ["command"]
    })JSON";
    return s;
}

std::future<ToolResult> BashTool::execute(ToolCall call) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string command = args["command"].get<std::string>();
            int timeout_ms = args.value("timeout_ms", 30000);

            static const std::vector<std::string> dangerous = {
                "rm -rf /", "mkfs.", "dd if=", ":(){ :|:& };:",
                "> /dev/sda", "chmod 777 /"
            };
            for (auto& d : dangerous) {
                if (command.find(d) != std::string::npos) {
                    result.is_error = true;
                    result.output = "Dangerous command rejected: pattern '" + d + "' detected";
                    return result;
                }
            }

            // 去重缓存：相同命令 60s 内不重复执行
            struct CacheEntry {
                std::string output;
                std::chrono::steady_clock::time_point timestamp;
            };
            static std::map<std::string, CacheEntry> dedup_cache;
            static std::mutex cache_mutex;
            static constexpr int kCacheTTLSeconds = 60;

            {
                std::lock_guard<std::mutex> lock(cache_mutex);
                auto it = dedup_cache.find(command);
                if (it != dedup_cache.end()) {
                    auto age = std::chrono::steady_clock::now() - it->second.timestamp;
                    if (age < std::chrono::seconds(kCacheTTLSeconds)) {
                        result.output = it->second.output + "\n[cached]";
                        return result;
                    }
                }
            }

            int pipefd[2];
            if (pipe(pipefd) < 0) {
                result.is_error = true;
                result.output = "Failed to create pipe";
                return result;
            }

            pid_t pid = fork();
            if (pid < 0) {
                close(pipefd[0]); close(pipefd[1]);
                result.is_error = true;
                result.output = "Failed to fork";
                return result;
            }

            if (pid == 0) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
                _exit(127);
            }

            close(pipefd[1]);
            std::array<char, 65536> buf{};
            std::string output;

            auto deadline = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(timeout_ms);

            while (true) {
                auto now = std::chrono::steady_clock::now();
                if (now >= deadline) {
                    kill(pid, SIGKILL);
                    waitpid(pid, nullptr, 0);
                    result.is_error = true;
                    result.output = output + "\n[Timeout after " +
                        std::to_string(timeout_ms) + "ms]";
                    close(pipefd[0]);
                    return result;
                }

                int remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - now).count();
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(pipefd[0], &fds);
                struct timeval tv = {remaining_ms / 1000, (remaining_ms % 1000) * 1000};

                int ret = select(pipefd[0] + 1, &fds, nullptr, nullptr, &tv);
                if (ret > 0) {
                    ssize_t n = read(pipefd[0], buf.data(), buf.size() - 1);
                    if (n > 0) {
                        buf[n] = '\0';
                        output += buf.data();
                    } else {
                        break;
                    }
                } else if (ret == 0) {
                    continue;
                } else {
                    break;
                }
            }

            close(pipefd[0]);
            int status;
            waitpid(pid, &status, 0);
            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

            if (exit_code != 0) {
                result.output = output + "\n[exit code: " + std::to_string(exit_code) + "]";
            } else {
                result.output = output.empty() ? "(no output)" : output;
            }
            // 存入去重缓存
            {
                std::lock_guard<std::mutex> lock(cache_mutex);
                dedup_cache[command] = {result.output, std::chrono::steady_clock::now()};
            }
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("Bash error: ") + e.what();
        }

        return result;
    });
}

} // namespace merak::tools
