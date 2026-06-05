#pragma once
#include <merak/message.hpp>
#include <string>
#include <vector>

namespace merak {

// Compresses tool result content in-place, replacing large results with
// placeholder text. Never deletes messages — only replaces content.
// Modeled after Astra's microcompact: preserves message identity and
// tool_call_id pairing while freeing token budget.
class ToolResultCompactor {
public:
    struct Config {
        int keep_recent = 6;           // keep N most recent tool results intact
        int max_result_chars = 8000;   // compress results exceeding this length
        double pressure_threshold = 0.6; // only compact when context pressure > this
    };

    explicit ToolResultCompactor(Config config = {}) : config_(config) {}

    // Compress tool result messages in-place. Returns number compacted.
    int compact(std::vector<Message>& history, double context_pressure);

private:
    Config config_;

    static bool is_compactable(const std::string& tool_name);
    static std::string make_placeholder(const Message& msg);
};

} // namespace merak
