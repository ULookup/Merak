#include <merak/compactor.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

namespace merak {

Compactor::Compactor(
    std::shared_ptr<LlmProvider> summary_llm,
    std::shared_ptr<TokenCounter> counter,
    std::string model)
    : summary_llm_(std::move(summary_llm))
    , counter_(std::move(counter))
    , model_(std::move(model))
{
}

std::future<CompactionResult> Compactor::compact(
    const std::vector<Message>& messages,
    int target_tokens
) {
    return std::async(std::launch::async, [this, messages, target_tokens]()
        -> CompactionResult
    {
        CompactionResult result;
        result.tokens_before = counter_->count(messages);
        result.replaced = messages;

        std::string history_text = messages_to_text(messages);
        if (history_text.empty()) {
            result.summary = "(empty history)";
            result.tokens_after = 0;
            return result;
        }

        std::ostringstream prompt;
        prompt << "Please summarize the following conversation history "
               << "in no more than " << target_tokens << " tokens. "
               << "Focus on key decisions, important facts, and user preferences. "
               << "Ignore trivial back-and-forth.\n\n"
               << history_text;

        ChatRequest req;
        req.model = model_.empty() ? "gpt-4o-mini" : model_;
        req.messages.push_back({"system", "You are a concise summarizer. Output only the summary, no preamble."});
        req.messages.push_back({"user", prompt.str()});
        req.max_output_tokens = target_tokens;
        req.enable_cache = false;
        req.enable_thinking = false;

        std::string summary_text;
        auto future_resp = summary_llm_->chat(req, [&](StreamChunk chunk) {
            if (!chunk.is_final && !chunk.is_tool_call) {
                summary_text += chunk.text;
            }
        });

        auto resp = future_resp.get();
        result.summary = resp.text;
        result.tokens_after = counter_->count(result.summary);

        spdlog::info("Compactor: {} messages → {} tokens summary ({} → {})",
            messages.size(), result.tokens_after,
            result.tokens_before, result.tokens_after);

        return result;
    });
}

std::future<CompactionResult> Compactor::compact_history(
    const std::vector<Message>& history,
    int keep_recent
) {
    if ((int)history.size() <= keep_recent) {
        return std::async(std::launch::deferred, []() -> CompactionResult {
            CompactionResult empty;
            empty.tokens_before = 0;
            empty.tokens_after = 0;
            return empty;
        });
    }

    int compress_end = (int)history.size() - keep_recent;
    std::vector<Message> to_compress(history.begin(), history.begin() + compress_end);

    return compact(to_compress);
}

std::string Compactor::messages_to_text(
    const std::vector<Message>& messages
) const {
    std::ostringstream oss;
    for (auto& msg : messages) {
        if (msg.role == "system") continue;

        oss << "[" << msg.role << "]: ";
        if (!msg.content.empty()) {
            oss << msg.content.substr(0, 500);
        }
        if (!msg.tool_calls.empty()) {
            oss << " [tool_calls: ";
            for (auto& tc : msg.tool_calls) {
                oss << tc.name << " ";
            }
            oss << "]";
        }
        if (msg.role == "tool") {
            oss << " [tool result: "
                << msg.content.substr(0, 200) << "]";
        }
        oss << "\n";
    }
    return oss.str();
}

std::future<std::string> Compactor::compact_one_round(
    const std::vector<Message>& round_messages) {
  return std::async(std::launch::async, [this, round_messages]() -> std::string {
    if (round_messages.empty()) return "";
    auto text = messages_to_text(round_messages);
    // Compact to a concise single-round summary (200 tokens target)
    auto result = compact({Message{"user", text}}, 200);
    return result.get().summary;
  });
}

} // namespace merak
