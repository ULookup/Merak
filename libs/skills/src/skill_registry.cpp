#include <merak/skills/skill_registry.hpp>
#include <merak/skills/skill_loader.hpp>

namespace merak::skills {

void SkillRegistry::discover_from(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        return;
    }

    for (auto const& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().filename() == "SKILL.md") {
            auto skill_opt = SkillLoader::load(entry.path());
            if (skill_opt.has_value()) {
                // First discover wins: only add if not already present
                if (skills_.find(skill_opt->name) == skills_.end()) {
                    skills_[skill_opt->name] = std::move(skill_opt.value());
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

} // namespace merak::skills
