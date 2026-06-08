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
};

} // namespace merak::skills
