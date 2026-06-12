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

class ToolRegistry;
class LlmProvider;
class MemoryStore;

void register_fork_skills(
    const SkillRegistry& registry,
    std::shared_ptr<ToolRegistry> tools,
    std::shared_ptr<LlmProvider> llm,
    std::shared_ptr<MemoryStore> memory);

} // namespace merak::skills
