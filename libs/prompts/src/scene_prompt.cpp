#include <merak/prompts/scene_prompt.hpp>
#include <sstream>

namespace merak::prompts {

namespace {

std::string join_names(const std::vector<std::string>& names) {
    std::ostringstream oss;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) oss << "、";
        oss << names[i];
    }
    return oss.str();
}

std::string build_foreshadowing_list(const std::vector<std::string>& items) {
    if (items.empty()) return "";
    std::ostringstream oss;
    oss << "### 相关伏笔（需关注）\n";
    for (const auto& item : items) {
        oss << "- " << item << "\n";
    }
    return oss.str();
}

std::string build_secrets_list(const std::vector<std::string>& items) {
    if (items.empty()) return "";
    std::ostringstream oss;
    oss << "### 进行中的秘密\n";
    for (const auto& item : items) {
        oss << "- " << item << "\n";
    }
    return oss.str();
}

std::string build_memories_list(const std::vector<std::string>& items) {
    if (items.empty()) return "";
    std::ostringstream oss;
    oss << "### 你最近的记忆\n";
    for (const auto& item : items) {
        oss << "- " << item << "\n";
    }
    return oss.str();
}

std::string build_tools_list(const std::vector<std::string>& names) {
    if (names.empty()) return "";
    std::ostringstream oss;
    oss << "### 你可以用的工具\n";
    for (const auto& name : names) {
        oss << "- " << name << "\n";
    }
    return oss.str();
}

} // namespace

PromptSection build_god_scene_context(const SceneContext& ctx) {
    std::ostringstream oss;
    oss << "## 当前场景上下文\n\n"
        << "**场景**：" << ctx.scene_title << " — " << ctx.scene_narrative << "\n"
        << "**世界时间**：" << ctx.world_time_label << "\n"
        << "**参与角色**：" << join_names(ctx.participant_names) << "\n\n";

    std::string foreshadowing = build_foreshadowing_list(ctx.relevant_foreshadowing);
    if (!foreshadowing.empty()) oss << foreshadowing << "\n";

    std::string secrets = build_secrets_list(ctx.known_secrets);
    if (!secrets.empty()) oss << secrets << "\n";

    return {oss.str(), PromptCachePolicy::None};
}

PromptSection build_character_scene_context(const SceneContext& ctx) {
    std::ostringstream oss;
    oss << "## 当前场景\n\n";

    if (!ctx.location.empty()) {
        oss << "你现在在 **" << ctx.location << "**";
    }
    if (!ctx.scene_narrative.empty()) {
        oss << "，场景：" << ctx.scene_narrative;
    }
    oss << "\n";

    if (!ctx.participant_names.empty()) {
        oss << "在场角色：" << join_names(ctx.participant_names) << "\n";
    }
    oss << "\n";

    std::string memories = build_memories_list(ctx.recent_memories);
    if (!memories.empty()) oss << memories << "\n";

    std::string foreshadowing = build_foreshadowing_list(ctx.relevant_foreshadowing);
    if (!foreshadowing.empty()) oss << foreshadowing << "\n";

    std::string tools = build_tools_list(ctx.tool_names);
    if (!tools.empty()) oss << tools << "\n";

    return {oss.str(), PromptCachePolicy::None};
}

PromptSection build_manager_scene_context(const SceneContext& ctx) {
    std::ostringstream oss;
    oss << "## 当前场景上下文\n\n"
        << "**场景**：" << ctx.scene_title << "\n"
        << "**世界时间**：" << ctx.world_time_label << "\n\n";

    std::string foreshadowing = build_foreshadowing_list(ctx.relevant_foreshadowing);
    if (!foreshadowing.empty()) oss << foreshadowing << "\n";

    std::string secrets = build_secrets_list(ctx.known_secrets);
    if (!secrets.empty()) oss << secrets << "\n";

    return {oss.str(), PromptCachePolicy::None};
}

} // namespace merak::prompts
