#include <merak/skills/skill_executor.hpp>
#include <sstream>

namespace merak::skills {

std::string SkillExecutor::expand(const SkillDef& skill) {
    std::ostringstream oss;
    oss << "## 技能：" << skill.name << " v" << skill.version << "\n\n";
    oss << skill.body;
    if (!skill.allowed_tools.empty()) {
        oss << "\n\n### 可用工具\n";
        for (const auto& tool : skill.allowed_tools) {
            oss << "- `" << tool << "`\n";
        }
    }
    return oss.str();
}

} // namespace merak::skills
