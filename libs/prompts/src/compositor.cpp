#include <merak/prompts/compositor.hpp>
#include <merak/prompts/core_prompt.hpp>
#include <merak/prompts/memory_prompt.hpp>
#include <merak/prompts/skill_prompt.hpp>
#include <merak/prompts/team_prompt.hpp>
#include <merak/prompts/scene_prompt.hpp>
#include <merak/dsl/parser.hpp>
#include <merak/dsl/resolver.hpp>
#include <merak/dsl/renderer.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>

namespace merak::prompts {

void PromptCompositor::sort_by_scope(std::vector<PromptSection>& sections) {
    std::stable_sort(sections.begin(), sections.end(),
        [](const PromptSection& a, const PromptSection& b) {
            return static_cast<int>(a.cache_policy) < static_cast<int>(b.cache_policy);
        });
}

void PromptCompositor::add_core(std::vector<PromptSection>& sections,
                                 const PromptProfile& profile) {
    auto core = build_core_sections(profile);
    sections.insert(sections.end(), core.begin(), core.end());
}

void PromptCompositor::add_memory(std::vector<PromptSection>& sections,
                                   const PromptProfile& profile) {
    // 仅 Platform Agent 使用 memory 规则
    if (profile.category != AgentCategory::Platform) return;

    auto section = build_memory_section(profile.memory_mode);
    if (!section.text.empty()) {
        sections.push_back(std::move(section));
    }
}

void PromptCompositor::add_skills(std::vector<PromptSection>& sections,
                                   const PromptProfile& profile) {
    if (!profile.include_skills) return;

    auto section = build_skill_section();
    if (!section.text.empty()) {
        sections.push_back(std::move(section));
    }
}

void PromptCompositor::add_team(std::vector<PromptSection>& sections,
                                 const PromptProfile& profile) {
    if (!profile.team_ctx.has_value()) return;

    std::string coord = build_team_coordination(*profile.team_ctx);
    if (!coord.empty()) {
        sections.push_back({coord, PromptCachePolicy::None});
    }
}

void PromptCompositor::add_scene(std::vector<PromptSection>& sections,
                                  const PromptProfile& profile) {
    if (!profile.scene_ctx.has_value()) return;
    if (profile.category != AgentCategory::Worldbuilding) return;

    const auto& ctx = *profile.scene_ctx;

    // 根据 AgentKind 选择不同场景上下文格式
    // AgentKind: 0=God, 1=MapManager, 2=HistoryManager,
    //            3=MagicSystemManager, 4=FactionManager,
    //            5=Individual, 6=Group
    int kind = profile.worldbuilding_kind.value_or(5); // 默认 Individual

    PromptSection section;
    if (kind == 0) {
        section = build_god_scene_context(ctx);
    } else if (kind >= 1 && kind <= 4) {
        section = build_manager_scene_context(ctx);
    } else {
        section = build_character_scene_context(ctx);
    }

    if (!section.text.empty()) {
        sections.push_back(std::move(section));
    }
}

void PromptCompositor::add_budget(std::vector<PromptSection>& sections,
                                   const PromptProfile& profile) {
    if (!profile.budget.has_value()) return;

    std::string budget_text = build_budget_awareness(*profile.budget);
    if (!budget_text.empty()) {
        sections.push_back({budget_text, PromptCachePolicy::None});
    }
}

std::string PromptCompositor::assemble(const PromptProfile& profile) {
    std::vector<PromptSection> sections;

    add_core(sections, profile);
    add_memory(sections, profile);
    add_skills(sections, profile);
    add_team(sections, profile);
    add_scene(sections, profile);
    add_budget(sections, profile);

    sort_by_scope(sections);

    std::ostringstream oss;
    for (size_t i = 0; i < sections.size(); ++i) {
        if (i > 0) oss << "\n\n";
        oss << sections[i].text;
    }

    std::string result = oss.str();

    // Resolve DSL references if worldbuilding service is available
    if (worldbuilding_service_ && profile.active_world_id.has_value()) {
        result = resolve_dsl_references(
            result, *worldbuilding_service_,
            *profile.active_world_id,
            profile.active_agent_id.value_or(""));
    }

    return result;
}

std::string PromptCompositor::resolve_dsl_references(
    const std::string& assembled_text,
    worldbuilding::WorldbuildingService& svc,
    const std::string& world_id,
    const std::string& agent_id) {

    if (world_id.empty()) return assembled_text;

    // Parse DSL references
    auto refs = dsl::Parser::parse(assembled_text);
    if (refs.empty()) return assembled_text;

    // Resolve each reference
    dsl::Resolver resolver(svc, world_id);
    if (!agent_id.empty()) {
        resolver.set_context("", "", "", agent_id);
    }

    std::vector<dsl::ResolvedContent> resolved;
    for (const auto& ref : refs) {
        try {
            resolved.push_back(resolver.resolve(ref));
        } catch (const std::exception& e) {
            spdlog::debug("DSL resolution failed for ref '{}': {}", ref.raw, e.what());
            // If resolution fails, leave the reference as-is
            resolved.push_back({ref.raw, ref.raw});
        }
    }

    // Render: replace references with resolved content
    return dsl::Renderer::render(assembled_text, resolved);
}

} // namespace merak::prompts
