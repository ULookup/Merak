#pragma once
#include <merak/skills/skill_types.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

namespace merak::skills {

class SkillRegistry {
    std::unordered_map<std::string, SkillDef> skills_;
public:
    // Recursively scan directory for SKILL.md files and load them
    void discover_from(const std::filesystem::path& dir);

    // Get a skill by name
    std::optional<SkillDef> get(const std::string& name) const;

    // List all registered skills
    std::vector<SkillDef> list() const;

    // Add a skill directly (higher priority wins on name collision)
    void add(SkillDef skill);

    std::vector<SkillDef> inline_skills() const;
    std::vector<SkillDef> fork_skills() const;
};

} // namespace merak::skills

// These types live in merak, not merak::skills
namespace merak {
class ToolRegistry;
class LlmProvider;
class MemoryStore;
} // namespace merak

namespace merak::skills {

void register_fork_skills(
    const SkillRegistry& registry,
    std::shared_ptr<merak::ToolRegistry> tools,
    std::shared_ptr<merak::LlmProvider> llm,
    std::shared_ptr<merak::MemoryStore> memory,
    std::string default_model);

} // namespace merak::skills
