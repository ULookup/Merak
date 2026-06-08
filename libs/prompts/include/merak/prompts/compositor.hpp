#pragma once
#include <merak/prompts/types.hpp>
#include <string>
#include <vector>

namespace merak::worldbuilding { class WorldbuildingService; }

namespace merak::prompts {

class PromptCompositor {
public:
    // 主入口：根据 profile 组装完整 system prompt
    std::string assemble(const PromptProfile& profile);

    // DSL 上下文注入：解析系统提示中的 @xxx{...} 引用并替换为实际内容
    std::string resolve_dsl_references(const std::string& assembled_text,
                                        worldbuilding::WorldbuildingService& svc,
                                        const std::string& world_id,
                                        const std::string& agent_id = "");

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
