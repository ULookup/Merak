#include <merak/shell_tool.hpp>
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
#include <mutex>
#include <map>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace merak::tools {

// ========== BashTool ==========

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

// ---------- 5-Layer Safety Check ----------

static std::string safety_check_layer_1(const std::string& command) {
    // Layer 1: Dangerous patterns blacklist
    static const std::vector<std::pair<std::string, std::string>> dangerous = {
        {"rm -rf /", "rm -rf / (recursive force remove root)"},
        {"mkfs.", "mkfs (filesystem format)"},
        {"dd if=* of=/dev/", "dd writing to raw device"},
        {"chmod 777 /", "chmod 777 on root"},
        {"> /dev/sda", "overwrite raw block device"},
        {"curl | sudo", "curl piped to sudo"},
        {"base64 -d | sh", "base64 decode piped to shell"},
        {"curl | bash", "curl piped to bash"},
        {"wget | sh", "wget piped to shell"},
        {"> /etc/", "redirect overwrite to /etc/"},
    };
    for (auto& d : dangerous) {
        if (command.find(d.first) != std::string::npos) {
            return "Layer 1: " + d.second;
        }
    }
    return "";
}

static std::string safety_check_layer_2(const std::string& command) {
    // Layer 2: Git destructive operations on main/master
    static const std::vector<std::regex> git_dangerous = {
        std::regex(R"(git\s+push\s+.*--force.*\b(main|master)\b)", std::regex::ECMAScript | std::regex::icase),
        std::regex(R"(git\s+reset\s+--hard)", std::regex::ECMAScript | std::regex::icase),
        std::regex(R"(git\s+clean\s+-fdx)", std::regex::ECMAScript | std::regex::icase),
        std::regex(R"(git\s+clean\s+-xdf)", std::regex::ECMAScript | std::regex::icase),
    };
    for (auto& re : git_dangerous) {
        if (std::regex_search(command, re)) {
            return "Layer 2: Destructive git operation detected";
        }
    }
    return "";
}

static std::string safety_check_layer_3(const std::string& command) {
    // Layer 3: Variable bypass — dangerous words inside $() or backticks
    static const std::vector<std::string> dangerous_words = {
        "rm", "mkfs", "dd", "chmod", ">", "sudo"
    };
    // Match $() or backtick command substitution
    std::regex sub_re(R"(\$\(([^)]*)\)|`([^`]*)`)", std::regex::ECMAScript);
    auto words_begin = std::sregex_iterator(command.begin(), command.end(), sub_re);
    auto words_end = std::sregex_iterator();
    for (auto it = words_begin; it != words_end; ++it) {
        std::string inner = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
        for (auto& word : dangerous_words) {
            if (inner.find(word) != std::string::npos) {
                return "Layer 3: Dangerous word '" + word +
                       "' detected inside substitution: " + inner;
            }
        }
    }
    return "";
}

static std::string safety_check_layer_4(const std::string& command) {
    // Layer 4: SQL destruction (case-insensitive)
    std::regex sql_re(
        R"(\b(DROP\s+(TABLE|DATABASE|SCHEMA))\b|\b(TRUNCATE\s+TABLE)\b|\b(DELETE\s+FROM\s+\w+(?!\s+WHERE))\b)",
        std::regex::ECMAScript | std::regex::icase);
    if (std::regex_search(command, sql_re)) {
        return "Layer 4: Destructive SQL operation detected (DROP/TRUNCATE/DELETE without WHERE)";
    }
    return "";
}

static std::string safety_check_layer_5(const std::string& command) {
    // Layer 5: Sandbox path — output redirect targeting outside cwd
    // Allow /dev/null and /tmp
    std::regex redirect_re(R"(\d*[>&]+\s*(/[/\w.\-]*))", std::regex::ECMAScript);
    auto words_begin = std::sregex_iterator(command.begin(), command.end(), redirect_re);
    auto words_end = std::sregex_iterator();
    for (auto it = words_begin; it != words_end; ++it) {
        std::string path = (*it)[1].str();
        if (path == "/dev/null" || path.rfind("/tmp", 0) == 0) {
            continue;  // allowed
        }
        // Check if outside cwd
        fs::path target(path);
        fs::path cwd = fs::current_path();
        // If target starts with / and is not under cwd, reject
        if (target.is_absolute()) {
            auto [cwd_end, _] = std::mismatch(cwd.begin(), cwd.end(), target.begin());
            if (cwd_end != cwd.end()) {
                // target is outside cwd
                return "Layer 5: Output redirect targets path outside working directory: " + path;
            }
        }
        // If target traverses above cwd via ..
        std::string path_str = path;
        if (path_str.find("..") != std::string::npos) {
            return "Layer 5: Output redirect uses '..' traversal: " + path_str;
        }
    }
    return "";
}

static std::string safety_check(const std::string& command) {
    std::string result;
    result = safety_check_layer_1(command);
    if (!result.empty()) return result;
    result = safety_check_layer_2(command);
    if (!result.empty()) return result;
    result = safety_check_layer_3(command);
    if (!result.empty()) return result;
    result = safety_check_layer_4(command);
    if (!result.empty()) return result;
    result = safety_check_layer_5(command);
    if (!result.empty()) return result;
    return "";
}

// ---------- Read-only cache helpers ----------

bool BashTool::is_safe_readonly(const std::string& command) {
    static const std::vector<std::string> safe_prefixes = {
        "ls ", "find ", "cat ", "head ", "tail ",
        "wc ", "du ", "stat ", "file ", "pwd"
    };
    // Also match bare commands without arguments
    static const std::vector<std::string> bare_safe = {
        "ls", "pwd"
    };
    for (auto& prefix : safe_prefixes) {
        if (command.rfind(prefix, 0) == 0) return true;
    }
    for (auto& bare : bare_safe) {
        if (command == bare) return true;
    }
    return false;
}

static std::vector<std::string> known_not_error_on_exit_1 = {
    "grep", "rg", "diff", "find"
};

static bool is_known_exit_1_tool(const std::string& command) {
    for (auto& prefix : known_not_error_on_exit_1) {
        if (command.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

// ---------- Read-only cache (static globals) ----------

struct CacheEntry {
    std::string output;
    int exit_code;
    std::chrono::steady_clock::time_point timestamp;
};

static std::map<std::string, CacheEntry> readonly_cache;
static std::mutex readonly_cache_mutex;
static constexpr int kCacheTTLSeconds = 60;

// ---------- Execute ----------

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

ToolMeta BashTool::meta() const {
    ToolMeta m;
    m.name = "execute_bash";
    m.description = "Execute shell commands for builds, tests, installs, git, CLI tasks";
    m.triggers = {"run", "execute", "build", "test", "install", "command", "shell"};
    m.pinned = true;
    m.intents = {IntentType::CodeEdit, IntentType::CodeRead, IntentType::Git};
    m.scope = Scope::Local;
    m.schema_tokens = 35;
    m.domain = ToolDomain::General;
    return m; // BashTool
}

std::future<ToolResult> BashTool::execute(ToolCall call, ToolExecutionContext context) {
    return std::async(std::launch::async, [call = std::move(call), context]() -> ToolResult {
        auto start_time = std::chrono::steady_clock::now();
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string command = args["command"].get<std::string>();
            int timeout_ms = args.value("timeout_ms", 30000);

            // ——— 5-Layer Safety Check ———
            std::string danger = safety_check(command);
            if (!danger.empty()) {
                result.is_error = true;
                result.output = "Dangerous command rejected: " + danger;
                result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                return result;
            }

            // ——— Read-only command cache (60s TTL) ———
            if (is_safe_readonly(command)) {
                std::lock_guard<std::mutex> lock(readonly_cache_mutex);
                auto it = readonly_cache.find(command);
                if (it != readonly_cache.end()) {
                    auto age = std::chrono::steady_clock::now() - it->second.timestamp;
                    if (age < std::chrono::seconds(kCacheTTLSeconds)) {
                        result.output = it->second.output;
                        result.exit_code = it->second.exit_code;
                        result.is_error = (it->second.exit_code != 0);
                        result.cached = true;
                        result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start_time).count();
                        return result;
                    }
                }
            }

            // ——— Fork / Exec / Pipe ———
            int pipefd[2];
            if (pipe(pipefd) < 0) {
                result.is_error = true;
                result.output = "Failed to create pipe";
                result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                return result;
            }

            pid_t pid = fork();
            if (pid < 0) {
                close(pipefd[0]); close(pipefd[1]);
                result.is_error = true;
                result.output = "Failed to fork";
                result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                return result;
            }

            if (pid == 0) {
                // Child process
                setpgid(0, 0);
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
                _exit(127);
            }

            // Parent process
            close(pipefd[1]);
            setpgid(pid, pid);
            std::array<char, 65536> buf{};
            std::string output;

            auto deadline = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(timeout_ms);

            while (true) {
                if (context.cancellation && context.cancellation->cancelled()) {
                    kill(-pid, SIGKILL);
                    waitpid(pid, nullptr, 0);
                    result.is_error = true;
                    result.output = output + "\n[Cancelled]";
                    close(pipefd[0]);
                    result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_time).count();
                    return result;
                }
                auto now = std::chrono::steady_clock::now();
                if (now >= deadline) {
                    kill(pid, SIGKILL);
                    waitpid(pid, nullptr, 0);
                    result.is_error = true;
                    result.output = output + "\n[Timeout after " +
                        std::to_string(timeout_ms) + "ms]";
                    close(pipefd[0]);
                    result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_time).count();
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

            // ——— Exit Code Semantics ———
            // grep, rg, diff, find: exit 1 means "no match" — not an error
            if (exit_code == 1 && is_known_exit_1_tool(command)) {
                result.is_error = false;
                result.exit_code = exit_code;
                result.output = output.empty() ? "(no output)" : output;
            } else if (exit_code != 0) {
                result.is_error = true;
                result.exit_code = exit_code;
                result.output = output + "\n[exit code: " + std::to_string(exit_code) + "]";
            } else {
                result.is_error = false;
                result.exit_code = 0;
                result.output = output.empty() ? "(no output)" : output;
            }

            // ——— Store in read-only cache ———
            if (is_safe_readonly(command)) {
                std::lock_guard<std::mutex> lock(readonly_cache_mutex);
                readonly_cache[command] = {result.output, exit_code, std::chrono::steady_clock::now()};
            }

            result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("Bash error: ") + e.what();
            result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
        }

        return result;
    });
}

} // namespace merak::tools
