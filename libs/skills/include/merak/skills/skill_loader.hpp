#pragma once
#include <merak/skills/skill_types.hpp>
#include <expected>
#include <filesystem>

namespace merak::skills {

class SkillLoader {
public:
    // Parse a SKILL.md file. Format:
    // ---
    // name: skill_name
    // version: 1.0.0
    // description: ...
    // allowed_tools:
    //   - tool_a
    //   - tool_b
    // context: inline
    // ---
    // Body starts here (Markdown)
    static std::expected<SkillDef, std::string> load(const std::filesystem::path& path);
};

} // namespace merak::skills
