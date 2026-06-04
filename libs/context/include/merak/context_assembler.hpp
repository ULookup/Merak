#pragma once
#include <merak/message.hpp>
#include <merak/token_counter.hpp>
#include <merak/memory_store.hpp>
#include <merak/prompts/types.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <vector>
#include <string>
#include <optional>

namespace merak {

struct TokenBudget {
    int model_max_tokens;
    double reserve_ratio = 0.15;
    double memory_ratio = 0.20;
};

struct CompactionResult {
    std::string summary;
    std::vector<Message> replaced;
    int tokens_before;
    int tokens_after;
};

class ContextAssembler {
public:
    ContextAssembler(TokenBudget budget,
        std::shared_ptr<TokenCounter> counter);

    std::vector<Message> assemble(
        const std::string& system_prompt,
        const nlohmann::json& tool_specs_json,
        const std::vector<Message>& history,
        const std::vector<MemorySnippet>& memory_snippets = {}
    );

    std::vector<Message> assemble(
        const prompts::PromptProfile& profile,
        const nlohmann::json& tool_specs_json,
        const std::vector<Message>& history,
        const std::vector<MemorySnippet>& memory_snippets = {}
    );

    struct CompactionNeeded {
        bool needed = false;
        int start_idx = 0;
        int end_idx = 0;
        int tokens_to_save;
    };

    CompactionNeeded analyze_compaction(
        const std::vector<Message>& history
    ) const;

    const TokenBudget& budget() const { return budget_; }
    int effective_budget() const;

private:
    TokenBudget budget_;
    std::shared_ptr<TokenCounter> counter_;

    Message build_system_message(const std::string& prompt) const;
    Message build_memory_message(
        const std::vector<MemorySnippet>& snippets) const;
};

} // namespace merak
