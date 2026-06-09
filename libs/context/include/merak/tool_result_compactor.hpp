#pragma once
#include <merak/message.hpp>
#include <merak/context_optimizer.hpp>
#include <string>
#include <vector>

namespace merak {

// Compresses tool result content in-place, replacing large results with
// placeholder text. Never deletes messages — only replaces content.
// Now delegates to ContextOptimizer::microcompact for the actual logic.
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
    // Delegates to ContextOptimizer::microcompact.
    int compact(std::vector<Message>& history, double context_pressure);

private:
    Config config_;
};

} // namespace merak
