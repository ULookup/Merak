#pragma once
#include <merak/message.hpp>
#include <string>
#include <vector>

namespace merak {

// Compresses tool result content in-place, replacing large results with
// placeholder text. Never deletes messages — only replaces content.
// Modeled after Astra's microcompact: preserves message identity and
// tool_call_id pairing while freeing token budget.
struct ToolResultCompactorConfig {
    int keep_recent = 6;
    int max_result_chars = 8000;
    double pressure_threshold = 0.6;
};

class ToolResultCompactor {
public:
    using Config = ToolResultCompactorConfig;

    ToolResultCompactor() = default;
    explicit ToolResultCompactor(Config config) : config_(config) {}

    // Compress tool result messages in-place. Returns number compacted.
    int compact(std::vector<Message>& history, double context_pressure);

private:
    Config config_;

    static bool is_compactable(const std::string& tool_name);
    static std::string make_placeholder(const Message& msg);
};

} // namespace merak
