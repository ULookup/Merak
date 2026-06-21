#include <merak/git_tool.hpp>

#include <nlohmann/json.hpp>
#include <array>
#include <cstdio>
#include <future>
#include <memory>
#include <string>

namespace merak::tools {

static std::string run_git(const std::string& args) {
    std::string cmd = "git " + args;
    std::array<char, 256> buffer;
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "error: popen failed";
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    int rc = pclose(pipe);
    if (rc != 0 && result.empty()) {
        result = "git command exited with code " + std::to_string(rc);
    }
    return result;
}

ToolSpec GitTool::spec() const {
    ToolSpec s;
    s.name = "git";
    s.description = "Git operations: status, diff, log, show, blame, file_history, commit";
    s.source = "builtin";
    s.category = Category::Mutating;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "description": "Git action: status, diff, log, show, blame, file_history, commit"
            },
            "path": {
                "type": "string",
                "description": "File path for diff, log, show, blame"
            },
            "branch": {
                "type": "string",
                "description": "Branch name for log"
            },
            "message": {
                "type": "string",
                "description": "Commit message"
            }
        },
        "required": ["action"]
    })";
    return s;
}

ToolMeta GitTool::meta() const {
    ToolMeta m;
    m.name = "git";
    m.description = "Git operations: status, diff, log, show, blame, file_history, log_search, contributors, commit, revert_commit, stash";
    m.triggers = {"git", "commit", "diff", "blame", "file_history", "log", "status", "branch", "stash"};
    m.pinned = false;
    m.intents = {IntentType::Git};
    m.scope = Scope::LocalGit;
    m.schema_tokens = 50;
    m.domain = ToolDomain::General;
    return m; // GitTool
}

PermissionLevel GitTool::permission() const {
    return PermissionLevel::ask;
}

std::future<ToolResult> GitTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            auto action = args.value("action", "");

            if (action == "status") {
                result.output = run_git("status --short");
            } else if (action == "diff") {
                result.output = run_git("diff " + args.value("path", ""));
            } else if (action == "log") {
                auto branch = args.value("branch", "");
                result.output = run_git("log --oneline -20 " + branch);
            } else if (action == "show") {
                result.output = run_git("show " + args.value("revision", "HEAD"));
            } else if (action == "blame") {
                result.output = run_git("blame " + args.value("path", ""));
            } else if (action == "file_history") {
                result.output = run_git("log --follow --oneline " + args.value("path", ""));
            } else if (action == "commit") {
                result.output = run_git("commit -m \"" + args.value("message", "") + "\" " + args.value("path", "."));
            } else {
                result.output = "Unknown git action: " + action;
                result.is_error = true;
            }
        } catch (const std::exception& e) {
            result.output = std::string("Error: ") + e.what();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> GitTool::clone() const {
    return std::make_unique<GitTool>();
}

} // namespace merak::tools
