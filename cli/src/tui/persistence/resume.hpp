#pragma once
#include "transcript.hpp"
#include "../chat_timeline.hpp"
#include "../history_cell/history_cell.hpp"
#include <memory>

namespace merak::tui::persistence {

struct ResumeResult {
    ChatTimeline timeline;
    SessionMeta meta;
    size_t event_count = 0;
};

inline ResumeResult restore(const std::string& session_id) {
    ResumeResult result;
    auto events = read_events(session_id);

    for (auto& event : events) {
        std::visit([&](auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, SessionMeta>) {
                result.meta = e;
            } else if constexpr (std::is_same_v<T, UserEvent>) {
                result.timeline.commit(std::make_shared<UserCell>(e.text));
            } else if constexpr (std::is_same_v<T, AssistantEvent>) {
                auto cell = std::make_shared<AssistantCell>();
                cell->append(e.text);
                cell->finalize();
                result.timeline.commit(cell);
            } else if constexpr (std::is_same_v<T, ToolEvent>) {
                ToolCall call;
                call.name = e.tool_name;
                call.arguments = e.args.dump();
                auto cell = std::make_shared<ToolCell>(call);
                ToolResult tr;
                tr.output = e.output;
                tr.is_error = (e.status == ToolEvent::Status::error);
                cell->complete(tr);
                result.timeline.commit(cell);
            } else if constexpr (std::is_same_v<T, SystemEvent>) {
                bool is_error = (e.level == SystemEvent::Level::error);
                result.timeline.commit(std::make_shared<SystemCell>(e.text, is_error));
            } else if constexpr (std::is_same_v<T, TurnSummaryEvent>) {
                result.timeline.commit(std::make_shared<TurnSummaryCell>(
                    e.elapsed_ms, e.tokens_in, e.tokens_out, e.tool_count, 0, true));
            }
        }, event);
    }
    result.event_count = events.size();
    return result;
}

} // namespace merak::tui::persistence
