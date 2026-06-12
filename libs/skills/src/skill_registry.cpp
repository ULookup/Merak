#include <merak/skills/skill_registry.hpp>
#include <merak/skills/skill_loader.hpp>
#include <merak/tool_registry.hpp>
#include <merak/fork_skill_tool.hpp>
#include <spdlog/spdlog.h>

namespace merak::skills {

void SkillRegistry::discover_from(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        return;
    }

    for (auto const& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().filename() == "SKILL.md") {
            auto result = SkillLoader::load(entry.path());
            if (result) {
                // First discover wins: only add if not already present
                if (skills_.find(result->name) == skills_.end()) {
                    skills_[result->name] = std::move(*result);
                }
            }
        }
    }
}

std::optional<SkillDef> SkillRegistry::get(const std::string& name) const {
    auto it = skills_.find(name);
    if (it != skills_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<SkillDef> SkillRegistry::list() const {
    std::vector<SkillDef> result;
    result.reserve(skills_.size());
    for (const auto& [name, skill] : skills_) {
        result.push_back(skill);
    }
    return result;
}

void SkillRegistry::add(SkillDef skill) {
    skills_[skill.name] = std::move(skill);
}

std::vector<SkillDef> SkillRegistry::inline_skills() const {
    std::vector<SkillDef> result;
    result.reserve(skills_.size());
    for (const auto& [name, skill] : skills_) {
        if (skill.context_mode.empty() || skill.context_mode == "inline") {
            result.push_back(skill);
        }
    }
    return result;
}

std::vector<SkillDef> SkillRegistry::fork_skills() const {
    std::vector<SkillDef> result;
    result.reserve(skills_.size());
    for (const auto& [name, skill] : skills_) {
        if (skill.context_mode == "fork") {
            result.push_back(skill);
        }
    }
    return result;
}

void register_fork_skills(
    const SkillRegistry& registry,
    std::shared_ptr<ToolRegistry> tools,
    std::shared_ptr<LlmProvider> llm,
    std::shared_ptr<MemoryStore> memory) {
    auto forks = registry.fork_skills();
    for (auto& def : forks) {
        auto tool = std::make_unique<tools::ForkSkillTool>(
            def, llm, tools, memory);
        tools->register_tool(std::move(tool));
        spdlog::info("Registered fork skill tool: skill:{}", def.name);
    }
}

} // namespace merak::skills
