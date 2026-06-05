#include <merak/prompts/core_prompt.hpp>
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>

namespace merak::prompts {

namespace {

std::string load_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        spdlog::warn("prompts: cannot load file {}", path);
        return "";
    }
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

std::string platform_role_addendum(PlatformRole role) {
    switch (role) {
    case PlatformRole::Explore:
        return "\n\n你是探索 Agent，专注于快速理解代码库。\n"
               "- 搜索和导航代码回答问题\n"
               "- 报告结果时标注文件路径和行号\n"
               "- 只读模式：不修改任何文件\n";
    case PlatformRole::CodeReview:
        return "\n\n你是代码审查 Agent，只反馈真正重要的问题。\n"
               "- 仅标记：bug、安全漏洞、逻辑错误\n"
               "- 不评论代码风格和格式\n"
               "- 只读模式：不修改任何文件\n";
    case PlatformRole::Task:
        return "\n\n你是任务执行 Agent，可靠地运行命令并报告结果。\n"
               "- 使用工具执行指定任务\n"
               "- 成功时：简要摘要\n"
               "- 失败时：输出相关错误信息\n";
    case PlatformRole::Core:
    default:
        return "";
    }
}

} // namespace

std::vector<PromptSection> build_core_sections(const PromptProfile& profile) {
    std::vector<PromptSection> sections;

    // L1: 核心角色定义（Global scope，可缓存）
    if (profile.category == AgentCategory::Platform) {
        std::string core = load_file("config/prompts/merak_core.md");
        if (!core.empty()) {
            sections.push_back({core, CacheScope::Global});
        }

        std::string addendum = platform_role_addendum(
            profile.platform_role.value_or(PlatformRole::Core));
        if (!addendum.empty()) {
            sections.push_back({addendum, CacheScope::Global});
        }
    }

    // L2: 行为规则（Session scope）
    if (profile.category == AgentCategory::Platform) {
        std::string behavior = load_file("config/prompts/rules/behavior.md");
        if (!behavior.empty()) {
            sections.push_back({behavior, CacheScope::Session});
        }

        std::string interaction = load_file("config/prompts/rules/interaction.md");
        if (!interaction.empty()) {
            sections.push_back({interaction, CacheScope::Session});
        }
    }

    return sections;
}

} // namespace merak::prompts
