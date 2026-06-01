#include <merak/token_counter.hpp>
#include <nlohmann/json.hpp>

namespace merak {

int TokenCounter::count(const Message& msg) const {
    int tokens = 0;
    tokens += 1; // role
    tokens += count(msg.content);

    for (auto& tc : msg.tool_calls) {
        tokens += 2;
        tokens += count(tc.arguments);
    }

    if (msg.tool_call_id.has_value()) {
        tokens += count(msg.tool_call_id.value());
    }

    return tokens;
}

int TokenCounter::count(const std::vector<Message>& messages) const {
    int total = 0;
    for (auto& msg : messages) {
        total += count(msg);
    }
    return total;
}

int TokenCounter::count(const std::string& text) const {
    if (text.empty()) return 0;
    return std::max(1, (int)(text.size() / chars_per_token_));
}

int TokenCounter::fit_in_budget(
    const std::vector<Message>& messages,
    int token_limit
) const {
    int total = 0;
    int kept = 0;

    for (int i = (int)messages.size() - 1; i >= 0; i--) {
        int msg_tokens = count(messages[i]);
        if (total + msg_tokens > token_limit) break;
        total += msg_tokens;
        kept++;
    }

    return kept;
}

} // namespace merak
