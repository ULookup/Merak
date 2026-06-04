#pragma once
#include <merak/prompts/types.hpp>
#include <string>
#include <vector>

namespace merak::prompts {

class PromptCompositor {
public:
    // 主入口：根据 profile 组装完整 system prompt
    std::string assemble(const PromptProfile& profile);

private:
    void add_core(std::vector<PromptSection>& sections, const PromptProfile& profile);
    void add_memory(std::vector<PromptSection>& sections, const PromptProfile& profile);
    void add_skills(std::vector<PromptSection>& sections, const PromptProfile& profile);
    void add_team(std::vector<PromptSection>& sections, const PromptProfile& profile);
    void add_scene(std::vector<PromptSection>& sections, const PromptProfile& profile);
    void add_budget(std::vector<PromptSection>& sections, const PromptProfile& profile);

    // 按 CacheScope 排序（Global < Session < None）
public:
    static void sort_by_scope(std::vector<PromptSection>& sections);
};

} // namespace merak::prompts
