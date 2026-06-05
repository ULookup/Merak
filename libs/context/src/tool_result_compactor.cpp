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

int ToolResultCompactor::compact(
    std::vector<Message>& history, double pressure) {
    if (pressure < config_.pressure_threshold) return 0;

    int compacted = 0;
    int recent_count = 0;

    for (int i = (int)history.size() - 1; i >= 0; i--) {
        auto& msg = history[i];
        if (msg.role != "tool") continue;
        if ((int)msg.content.size() <= config_.max_result_chars) continue;

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
