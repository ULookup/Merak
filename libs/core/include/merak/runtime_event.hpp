#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace merak {

struct RuntimeEvent {
    long long seq = 0;
    std::string timestamp;
    std::string session_id;
    std::string run_id;
    std::string type;
    nlohmann::json payload = nlohmann::json::object();
};

inline void to_json(nlohmann::json& j, const RuntimeEvent& event) {
    j = {
        {"seq", event.seq}, {"timestamp", event.timestamp},
        {"session_id", event.session_id}, {"run_id", event.run_id},
        {"type", event.type}, {"payload", event.payload},
    };
}

inline void from_json(const nlohmann::json& j, RuntimeEvent& event) {
    event.seq = j.value("seq", 0LL);
    event.timestamp = j.value("timestamp", "");
    event.session_id = j.value("session_id", "");
    event.run_id = j.value("run_id", "");
    event.type = j.value("type", "");
    event.payload = j.value("payload", nlohmann::json::object());
}

} // namespace merak
