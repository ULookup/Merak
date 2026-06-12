#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace merak::skills {

struct SkillDef {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> allowed_tools;
    std::string context_mode;     // "inline" | "fork"
    std::string body;
    std::filesystem::path source_path;
    int fork_max_turns = 5;       // fork 子 AgentLoop 最大轮数
    int fork_max_tokens = 16000;  // fork 子 AgentLoop token 预算
};

} // namespace merak::skills
