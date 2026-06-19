#pragma once
#include <merak/message.hpp>
#include <merak/llm_provider.hpp>
#include <merak/token_counter.hpp>
#include <merak/context_assembler.hpp>
#include <memory>
#include <vector>
#include <string>
#include <future>

namespace merak {

class Compactor {
public:
    Compactor(
        std::shared_ptr<LlmProvider> summary_llm,
        std::shared_ptr<TokenCounter> counter,
        std::string model = ""
    );

    std::future<CompactionResult> compact(
        const std::vector<Message>& messages,
        int target_tokens = 500
    );

    std::future<CompactionResult> compact_history(
        const std::vector<Message>& history,
        int keep_recent
    );

    std::future<std::string> compact_one_round(
        const std::vector<Message>& round_messages
    );

private:
    std::shared_ptr<LlmProvider> summary_llm_;
    std::shared_ptr<TokenCounter> counter_;
    std::string model_;

    std::string messages_to_text(const std::vector<Message>& messages) const;
};

} // namespace merak
