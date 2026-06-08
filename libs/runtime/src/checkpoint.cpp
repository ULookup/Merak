#include <merak/checkpoint.hpp>
#include <nlohmann/json.hpp>

namespace merak {

nlohmann::json to_json(const ToolCallRecord& tc) {
    return {
        {"id", tc.id},
        {"name", tc.name},
        {"arguments", tc.arguments},
        {"status", tc.status}
    };
}

nlohmann::json to_json(const RunCheckpoint& cp) {
    nlohmann::json pending = nlohmann::json::array();
    for (const auto& tc : cp.pending_calls) {
        pending.push_back(to_json(tc));
    }
    return {
        {"id", cp.id},
        {"run_id", cp.run_id},
        {"turn_index", cp.turn_index},
        {"turn_state", cp.turn_state},
        {"input_tokens_used", cp.input_tokens_used},
        {"output_tokens_used", cp.output_tokens_used},
        {"pending_calls", pending},
        {"compacted_history_summary", cp.compacted_history_summary},
        {"pipeline_snapshot_json", cp.pipeline_snapshot_json},
        {"created_at", cp.created_at}
    };
}

} // namespace merak
