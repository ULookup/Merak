#include <merak/tool_result_compactor.hpp>
#include <set>
#include <sstream>

namespace merak {

bool ToolResultCompactor::is_compactable(const std::string& tool_name) {
    static const std::set<std::string> non_compactable = {
        "bash", "write_file", "str_replace", "multi_edit",
        "delete_file", "skill", "delegate"
    };
    return non_compactable.find(tool_name) == non_compactable.end();
}

std::string ToolResultCompactor::make_placeholder(const Message& msg) {
    std::ostringstream oss;
    oss << "[已压缩] 工具结果过长，原始长度 "
        << msg.content.size()
        << " 字符。如需重新获取请重新调用工具。";
    return oss.str();
}

// Resolve tool name for a tool message by searching backwards for the
// preceding assistant message whose tool_calls contain the matching call_id.
static std::string resolve_tool_name(
    const std::vector<Message>& history, int tool_idx) {
    const auto& tool_msg = history[tool_idx];
    const auto& call_id = tool_msg.tool_call_id;
    for (int j = tool_idx - 1; j >= 1; j--) {
        if (history[j].role != "assistant") continue;
        for (auto& tc : history[j].tool_calls) {
            if (call_id && tc.id == *call_id) {
                return tc.name;
            }
        }
    }
    return "";
}

int ToolResultCompactor::compact(
    std::vector<Message>& history, double pressure) {
    if (pressure < config_.pressure_threshold) return 0;

    int compacted = 0;
    int recent_count = 0;

    for (int i = (int)history.size() - 1; i >= 0; i--) {
        auto& msg = history[i];
        if (msg.role != "tool") continue;
        if ((int)msg.content.size() <= config_.max_result_chars) continue;

        std::string name = resolve_tool_name(history, i);
        if (!is_compactable(name)) continue;

        if (recent_count < config_.keep_recent) {
            recent_count++;
            continue;
        }

        msg.content = make_placeholder(msg);
        compacted++;
    }

    return compacted;
}

} // namespace merak
