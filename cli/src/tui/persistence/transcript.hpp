#pragma once
#include "turn_event_json.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

namespace merak::tui::persistence {

namespace fs = std::filesystem;

inline fs::path transcript_dir() {
    if (auto* home = std::getenv("HOME"))
        return fs::path(home) / ".merak" / "transcripts";
    return fs::path(".merak") / "transcripts";
}

inline fs::path transcript_path(const std::string& session_id) {
    return transcript_dir() / (session_id + ".jsonl");
}

inline fs::path index_path() {
    return transcript_dir() / "index.json";
}

inline void append_event(const std::string& session_id, const TurnEvent& event) {
    fs::create_directories(transcript_dir());
    auto j = to_json(event);
    std::ofstream out(transcript_path(session_id), std::ios::app);
    out << j.dump() << '\n';
}

inline std::vector<TurnEvent> read_events(const std::string& session_id) {
    std::vector<TurnEvent> events;
    std::ifstream in(transcript_path(session_id));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        try {
            events.push_back(from_json(nlohmann::json::parse(line)));
        } catch (const std::exception&) {
        }
    }
    return events;
}

inline void update_index(const std::string& session_id, const SessionMeta& meta) {
    fs::create_directories(transcript_dir());
    nlohmann::json index = nlohmann::json::array();
    if (fs::exists(index_path())) {
        std::ifstream in(index_path());
        try { in >> index; } catch (...) {}
    }
    index.erase(std::remove_if(index.begin(), index.end(),
        [&](const auto& e) { return e.value("sid", "") == session_id; }), index.end());
    index.push_back({
        {"sid", session_id},
        {"ts", meta.created_at},
        {"model", meta.model},
        {"msg_count", 0},
        {"cwd", meta.cwd}
    });
    std::ofstream out(index_path());
    out << index.dump();
}

inline void delete_session(const std::string& session_id) {
    fs::remove(transcript_path(session_id));
    if (fs::exists(index_path())) {
        nlohmann::json index;
        {
            std::ifstream in(index_path());
            try { in >> index; } catch (...) {}
        }
        index.erase(std::remove_if(index.begin(), index.end(),
            [&](const auto& e) { return e.value("sid", "") == session_id; }), index.end());
        std::ofstream out(index_path());
        out << index.dump();
    }
}

} // namespace merak::tui::persistence
