#pragma once
#include <string>

namespace merak {

struct InterruptionRecord {
    int turns_completed = 0;
    int tools_completed = 0;
    int tools_remaining = 0;
    std::string interrupted_tool_name;
    std::string interrupted_tool_call_id;
};

} // namespace merak
