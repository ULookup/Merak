#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace merak {

struct ToolCallRecord {
    std::string id;
    std::string name;
    std::string arguments;
    std::string status;  // "pending" | "completed" | "error"
};

struct RunCheckpoint {
    std::string id;
    std::string run_id;
    int turn_index = 0;
    std::string turn_state;  // serialized TurnState JSON
    int64_t input_tokens_used = 0;
    int64_t output_tokens_used = 0;
    std::vector<ToolCallRecord> pending_calls;
    std::string compacted_history_summary;
    std::string pipeline_snapshot_json;  // serialized PipelineState
    std::string created_at;
};

} // namespace merak
