#include <merak/context_assembler.hpp>
#include <merak/prompts/compositor.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

namespace merak {

ContextAssembler::ContextAssembler(
    TokenBudget budget,
    std::shared_ptr<TokenCounter> counter)
    : budget_(std::move(budget))
    , counter_(std::move(counter))
{
}

std::vector<Message> ContextAssembler::assemble(
    const std::string& system_prompt,
    const nlohmann::json& tool_specs_json,
    const std::vector<Message>& history,
    const std::vector<MemorySnippet>& memory_snippets
) {
    std::vector<Message> result;

    result.push_back(build_system_message(system_prompt));

    int sys_tokens = counter_->count(result.back());
    spdlog::debug("Context: system_prompt = {} tokens", sys_tokens);

    if (!memory_snippets.empty()) {
        auto mem_msg = build_memory_message(memory_snippets);
        int mem_tokens = counter_->count(mem_msg);
        int max_mem_tokens = (int)(budget_.model_max_tokens * budget_.memory_ratio);

        if (mem_tokens <= max_mem_tokens) {
            result.push_back(std::move(mem_msg));
            spdlog::debug("Context: memory = {} tokens", mem_tokens);
        } else {
            spdlog::warn("Context: memory too large ({} > {}), truncated",
                mem_tokens, max_mem_tokens);
        }
    }

    int used = counter_->count(result);
    int available = effective_budget() - used;
    int keep_count = counter_->fit_in_budget(history, available);

    int start = std::max(0, (int)history.size() - keep_count);
    for (int i = start; i < (int)history.size(); i++) {
        result.push_back(history[i]);
    }

    int total = counter_->count(result);
    spdlog::info("Context: assembled {} messages, {} tokens (budget={})",
        result.size(), total, effective_budget());

    return result;
}

ContextAssembler::CompactionNeeded ContextAssembler::analyze_compaction(
    const std::vector<Message>& history
) const {
    CompactionNeeded result;
    int total_tokens = counter_->count(history);
    int available = effective_budget();
    int history_budget = available - 5000;

    if (total_tokens <= history_budget) {
        result.needed = false;
        return result;
    }

    result.needed = true;
    result.tokens_to_save = total_tokens - history_budget;

    int acc = 0;
    for (int i = 0; i < (int)history.size(); i++) {
        acc += counter_->count(history[i]);
        if (acc >= result.tokens_to_save) {
            result.end_idx = i + 1;
            break;
        }
    }

    spdlog::info("Context: compaction needed, save {} tokens, compress [0, {})",
        result.tokens_to_save, result.end_idx);
    return result;
}

int ContextAssembler::effective_budget() const {
    return (int)(budget_.model_max_tokens * (1.0 - budget_.reserve_ratio));
}

Message ContextAssembler::build_system_message(const std::string& prompt) const {
    Message msg;
    msg.role = "system";
    msg.content = prompt;
    return msg;
}

Message ContextAssembler::build_memory_message(
    const std::vector<MemorySnippet>& snippets
) const {
    std::ostringstream oss;
    oss << "以下是与本任务相关的历史信息（按相关性排序）:\n\n";

    for (int i = 0; i < (int)snippets.size(); i++) {
        oss << "【相关片段 " << (i + 1) << "】"
            << " (相关度: " << (int)(snippets[i].relevance * 100) << "%)\n"
            << snippets[i].content << "\n\n";
    }

    Message msg;
    msg.role = "user";
    msg.content = oss.str();
    return msg;
}

std::vector<Message> ContextAssembler::assemble(
    const prompts::PromptProfile& profile,
    const nlohmann::json& tool_specs_json,
    const std::vector<Message>& history,
    const std::vector<MemorySnippet>& memory_snippets
) {
    prompts::PromptCompositor compositor;
    std::string system_prompt = compositor.assemble(profile);

    return assemble(system_prompt, tool_specs_json, history, memory_snippets);
}

} // namespace merak
