#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace merak::prompts {

// ─── Cache 层级（对标 astra CacheScope）───
enum class CacheScope {
    Global,   // L1：跨会话稳定，可缓存
    Session,  // L2：会话内稳定
    None      // L3：每回合变化
};

// ─── Agent 大类 ───
enum class AgentCategory {
    Platform,
    Worldbuilding,
};

// ─── Platform Agent 子类型（对标 astra BuiltinAgentType）───
enum class PlatformRole {
    Core,
    Explore,
    CodeReview,
    Task,
};

// ─── Memory 规则模式（对标 astra MemoryPromptMode）───
enum class MemoryPromptMode {
    None,
    Minimal,
    Full,
};

// ─── 团队协作模式（对标 astra team_prompts）───
enum class CoordinationMode {
    FanOut,
    Pipeline,
    AdversarialProducer,
    AdversarialReviewer,
    Fork,
    None,
};

// ─── 团队协作上下文 ───
struct TeamContext {
    CoordinationMode mode = CoordinationMode::None;
    std::string agent_id;
    std::vector<std::string> sibling_ids;
    std::string aggregation_strategy = "AllResults";
    int stage_index = 0;
    int total_stages = 0;
    bool has_previous_output = false;
    bool has_feedback = false;
    bool has_gate = false;
    int current_round = 0;
    int max_rounds = 0;
};

// ─── 场景上下文 ───
struct SceneContext {
    std::string scene_title;
    std::string scene_narrative;
    std::string world_time_label;
    std::string location;
    std::vector<std::string> participant_names;
    std::vector<std::string> recent_memories;
    std::vector<std::string> relevant_foreshadowing;
    std::vector<std::string> known_secrets;
    std::vector<std::string> tool_names;
};

// ─── 资源约束（对标 astra budget_awareness_prompt）───
struct ResourceBudget {
    std::optional<uint64_t> max_tokens;
    std::optional<uint64_t> max_duration_secs;
};

// ─── Prompt 组装入口 ───
struct PromptProfile {
    AgentCategory category = AgentCategory::Platform;
    std::optional<PlatformRole> platform_role;
    std::optional<int> worldbuilding_kind; // 对应 AgentKind 的 int 值
    MemoryPromptMode memory_mode = MemoryPromptMode::Full;
    bool include_skills = true;
    std::optional<TeamContext> team_ctx;
    std::optional<SceneContext> scene_ctx;
    std::optional<ResourceBudget> budget;

    // DSL 上下文注入所需
    std::optional<std::string> active_world_id;
    std::optional<std::string> active_agent_id;
};

// ─── PromptSection（对标 astra PromptSection）───
struct PromptSection {
    std::string text;
    CacheScope scope = CacheScope::None;
};

} // namespace merak::prompts
