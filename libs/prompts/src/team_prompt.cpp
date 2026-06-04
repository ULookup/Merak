#include <merak/prompts/team_prompt.hpp>
#include <sstream>
#include <algorithm>

namespace merak::prompts {

namespace {

std::string build_fan_out(const TeamContext& ctx) {
    std::vector<std::string> siblings;
    for (const auto& id : ctx.sibling_ids) {
        if (id != ctx.agent_id) siblings.push_back(id);
    }

    std::ostringstream oss;
    oss << "## 团队协作：并行执行\n\n";

    if (siblings.empty()) {
        oss << "你是此任务的唯一 Agent。\n";
    } else {
        oss << "你正在与以下 Agent 并行工作：";
        for (size_t i = 0; i < siblings.size(); ++i) {
            if (i > 0) oss << "、";
            oss << siblings[i];
        }
        oss << "。\n每个 Agent 独立工作 —— 不要假设其他 Agent 会覆盖你跳过的部分。\n";
    }

    oss << "\n**聚合策略**：" << ctx.aggregation_strategy << "\n";
    if (ctx.aggregation_strategy == "FirstSuccess") {
        oss << "结果将按首次成功选取 —— 力求完整和自包含。\n";
    } else if (ctx.aggregation_strategy == "Consensus") {
        oss << "结果将按共识比较 —— 保持精确和基于证据。\n";
    } else if (ctx.aggregation_strategy == "LlmGuided") {
        oss << "LLM 将综合所有 Agent 输出 —— 用标题和关键发现清晰组织输出。\n";
    } else {
        oss << "所有 Agent 输出将被收集 —— 保持完整但避免冗余。\n";
    }

    if (ctx.has_gate) {
        oss << "\n质量门：你的输出将被自动验证。确保内容充实、不重复、直接回应任务。\n";
    }

    return oss.str();
}

std::string build_pipeline(const TeamContext& ctx) {
    std::string position;
    if (ctx.stage_index == 0) {
        position = "第一个";
    } else if (ctx.stage_index == ctx.total_stages - 1) {
        position = "最后一个";
    } else {
        position = "第 " + std::to_string(ctx.stage_index + 1) + "/"
                   + std::to_string(ctx.total_stages) + " 阶段";
    }

    std::ostringstream oss;
    oss << "## 团队协作：流水线（" << position << "）\n\n"
        << "你是 Agent **" << ctx.agent_id << "**，"
        << ctx.total_stages << " 阶段流水线的" << position << "。\n";

    if (ctx.has_previous_output) {
        oss << "上一阶段 Agent 的输出见下文。在其基础上构建 —— "
            << "不要重复已完成的工作。\n";
    } else {
        oss << "你是流水线的第一个阶段。产生清晰、结构化的输出供下游 Agent 使用。\n";
    }

    if (ctx.has_gate) {
        oss << "\n质量门活跃：输出在传递到下游前会被验证。\n";
    }

    return oss.str();
}

std::string build_adversarial_producer(const TeamContext& ctx) {
    std::ostringstream oss;
    oss << "## 团队协作：对抗式审查（生产者）\n\n";

    if (ctx.current_round == 0) {
        oss << "这是第 1/" << ctx.max_rounds << " 轮。"
            << "请产生你最好的成果 —— 审查者会评估它。\n";
    } else {
        oss << "这是第 " << (ctx.current_round + 1) << "/" << ctx.max_rounds << " 轮。"
            << "审查者的反馈见前文。逐条处理所有反馈。\n";
    }

    if (ctx.has_feedback) {
        oss << "\n**修订指导：**\n"
            << "- 仔细重读审查者的反馈\n"
            << "- 逐条明确回应每个问题\n"
            << "- 如果不同意某个建议，解释原因\n"
            << "- 产出完整的修订输出（不只是变更部分）\n";
    }

    if (ctx.has_gate) {
        oss << "\n质量门活跃：输出必须通过自动验证才能进入审查。\n";
    }

    return oss.str();
}

std::string build_adversarial_reviewer(const TeamContext& ctx) {
    std::ostringstream oss;
    oss << "## 团队协作：对抗式审查（审查者）\n\n"
        << "审查来自 **" << ctx.agent_id << "** 的输出"
        << "（第 " << (ctx.current_round + 1) << "/" << ctx.max_rounds << " 轮）。\n\n"
        << "**审查协议：**\n"
        << "1. 评估正确性 —— 有无事实错误或逻辑漏洞？\n"
        << "2. 评估完整性 —— 是否完全满足原始任务？\n"
        << "3. 评估质量 —— 结构清晰、可执行吗？\n"
        << "4. 提供具体、有建设性的反馈\n"
        << "5. 如果输出令人满意，明确说明\n\n"
        << "**输出格式：**\n"
        << "- 以裁决开头：APPROVE / NEEDS_REVISION / REJECT\n"
        << "- 列出具体问题（如有）及建议修复\n"
        << "- 精确 —— 模糊的反馈浪费修订轮次\n";

    return oss.str();
}

std::string build_fork(const TeamContext& ctx) {
    std::ostringstream oss;
    oss << "## 团队协作：分叉（子任务 #" << (ctx.stage_index + 1)
        << " / " << ctx.total_stages << "）\n\n"
        << "你是独立分叉，执行大任务的一部分。\n"
        << "- 直接执行分配给你的任务 —— 不要进一步委托\n"
        << "- 自包含输出\n"
        << "- 简洁但完整\n";

    return oss.str();
}

} // namespace

std::string build_team_coordination(const TeamContext& ctx) {
    switch (ctx.mode) {
    case CoordinationMode::FanOut:
        return build_fan_out(ctx);
    case CoordinationMode::Pipeline:
        return build_pipeline(ctx);
    case CoordinationMode::AdversarialProducer:
        return build_adversarial_producer(ctx);
    case CoordinationMode::AdversarialReviewer:
        return build_adversarial_reviewer(ctx);
    case CoordinationMode::Fork:
        return build_fork(ctx);
    case CoordinationMode::None:
    default:
        return "";
    }
}

std::string build_budget_awareness(const ResourceBudget& budget) {
    std::vector<std::string> parts;

    if (budget.max_tokens.has_value() && *budget.max_tokens > 0) {
        auto k = *budget.max_tokens / 1000;
        parts.push_back(
            "- Token 预算：约 " + std::to_string(k)
            + "K tokens 在所有团队 Agent 间共享。请高效使用。");
    }

    if (budget.max_duration_secs.has_value() && *budget.max_duration_secs > 0) {
        auto mins = *budget.max_duration_secs / 60;
        if (mins > 0) {
            parts.push_back(
                "- 时间预算：" + std::to_string(mins) + " 分钟。优先处理高影响事项。");
        } else {
            parts.push_back(
                "- 时间预算：" + std::to_string(*budget.max_duration_secs)
                + " 秒。极度聚焦。");
        }
    }

    if (parts.empty()) return "";

    std::ostringstream oss;
    oss << "## 资源约束\n";
    for (const auto& p : parts) oss << p << "\n";
    return oss.str();
}

} // namespace merak::prompts
