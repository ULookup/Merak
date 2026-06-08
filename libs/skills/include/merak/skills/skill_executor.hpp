#pragma once
#include <merak/skills/skill_types.hpp>
#include <string>

namespace merak::skills {

class SkillExecutor {
public:
    // Expand a skill's body into instructions for the current context.
    // For "inline": returns the body as-is, wrapped with skill name header.
    // For "fork" (future): would spawn sub-agent.
    static std::string expand(const SkillDef& skill);
};

} // namespace merak::skills
