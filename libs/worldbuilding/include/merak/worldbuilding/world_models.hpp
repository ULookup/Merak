#pragma once

#include <merak/kg/kg_models.hpp>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace merak::worldbuilding {

enum class AgentKind {
    God,
    MapManager,
    HistoryManager,
    MagicSystemManager,
    FactionManager,
    RelationManager,
    Individual,
    Group
};
enum class ChapterStatus { Outline, Drafting, Completed, Revised };
enum class SceneStatus { Draft, Writing, Completed };
enum class ForeshadowStatus { Open, Paid, Abandoned };
enum class ForeshadowHintLevel { Subtle, Visible, Obvious };
enum class ForeshadowCreatedBy { Author, GodAgentDetected };
enum class SecretStatus { Active, Exposed, Abandoned };
enum class NarrativeTemplate { ThreeAct, FourAct, HerosJourney, Freeform };
enum class KnowledgeState { Public, Secret, Unknown };

struct WorldMeta {
    std::string id, name, description, created_at, updated_at;
};

struct WorldKnowledge {
    std::string id, category, content, created_at;
    std::vector<std::string> tags;
    std::vector<std::string> aliases;
    std::vector<std::string> related_ids;
};

struct Location {
    std::string id;
    std::string name;
    std::string description;
    std::string region;
    std::optional<std::string> parent_location_id;
    std::string created_at;
};

struct WorldTime {
    int day = 1;
    int period = 0; // 0=晨 1=昼 2=午 3=晚 4=夜
    std::string label;

    bool operator<(const WorldTime& other) const {
        if (day != other.day) return day < other.day;
        return period < other.period;
    }
    bool operator<=(const WorldTime& other) const {
        return *this < other || (day == other.day && period == other.period);
    }

    static std::optional<WorldTime> parse(const std::string& input);
    std::string to_label() const;
};

inline std::optional<WorldTime> WorldTime::parse(const std::string& input) {
    if (input.empty()) return std::nullopt;

    WorldTime wt;

    // Duration format: \d+[hd] (e.g., "2h", "1d")
    if (input.size() >= 2 && std::isdigit(input[0])) {
        size_t i = 0;
        while (i < input.size() && std::isdigit(input[i])) i++;
        if (i < input.size() && (input[i] == 'h' || input[i] == 'd') && i == input.size() - 1) {
            int val;
            try { val = std::stoi(input.substr(0, i)); }
            catch (...) { return std::nullopt; }
            if (input[i] == 'd') { wt.day = val; wt.period = 0; }
            else { wt.day = 1; wt.period = std::clamp(val, 0, 4); }
            wt.label = input;
            return wt;
        }
        return std::nullopt;
    }

    // Chinese named format: 第X日[晨/昼/午/晚/夜] or X日[晨/昼/午/晚/夜]
    size_t day_pos = input.find('日');
    if (day_pos != std::string::npos) {
        // Extract day number
        size_t num_start = 0;
        if (day_pos > 0 && input[day_pos - 1] >= '0' && input[day_pos - 1] <= '9') {
            num_start = day_pos - 1;
            while (num_start > 0 && input[num_start - 1] >= '0' && input[num_start - 1] <= '9')
                num_start--;
            try { wt.day = std::stoi(input.substr(num_start, day_pos - num_start)); }
            catch (...) { return std::nullopt; }
        } else if (day_pos >= 3 && input.substr(day_pos - 3, 3) == "第一") {
            wt.day = 1;
        } else if (day_pos >= 3 && input.substr(day_pos - 3, 3) == "第二") {
            wt.day = 2;
        } else if (day_pos >= 3 && input.substr(day_pos - 3, 3) == "第三") {
            wt.day = 3;
        } else if (day_pos >= 3 && input.substr(day_pos - 3, 3) == "第四") {
            wt.day = 4;
        } else if (day_pos >= 3 && input.substr(day_pos - 3, 3) == "第五") {
            wt.day = 5;
        } else if (day_pos >= 3 && input.substr(day_pos - 3, 3) == "第六") {
            wt.day = 6;
        } else if (day_pos >= 3 && input.substr(day_pos - 3, 3) == "第七") {
            wt.day = 7;
        } else {
            wt.day = 1; // default
        }

        // Extract period from suffix
        if (input.find("晨") != std::string::npos) wt.period = 0;
        else if (input.find("昼") != std::string::npos) wt.period = 1;
        else if (input.find("午") != std::string::npos) wt.period = 2;
        else if (input.find("晚") != std::string::npos) wt.period = 3;
        else if (input.find("夜") != std::string::npos) wt.period = 4;
        else wt.period = 0;

        wt.label = input;
        return wt;
    }

    // English format: dayN_[dawn/morning/noon/evening/night]
    size_t underscore = input.find('_');
    if (underscore != std::string::npos && input.find("day") == 0) {
        try {
            wt.day = std::stoi(input.substr(3, underscore - 3));
        } catch (...) {
            return std::nullopt;
        }
        std::string period_str = input.substr(underscore + 1);
        if (period_str == "dawn" || period_str == "morning") wt.period = 0;
        else if (period_str == "noon" || period_str == "midday") wt.period = 2;
        else if (period_str == "evening") wt.period = 3;
        else if (period_str == "night") wt.period = 4;
        else wt.period = 1;

        wt.label = input;
        return wt;
    }

    return std::nullopt;
}

inline std::string WorldTime::to_label() const {
    if (!label.empty()) return label;
    static const char* periods[] = {"晨", "昼", "午", "晚", "夜"};
    return "第" + std::to_string(day) + "日" + periods[std::clamp(period, 0, 4)];
}

struct AgentRecord {
    std::string id, world_id, name, display_name, created_at, updated_at;
    AgentKind kind = AgentKind::Individual;
};

struct ManagerProfile {
    std::string agent_id, domain, instructions;
};

struct GroupProfile {
    std::string agent_id, culture_card_markdown;
    std::vector<std::string> member_agent_ids;
    std::vector<std::string> shared_memory_ids;
};

struct Arc {
    std::string id, title, purpose, status;
    std::vector<int> chapter_numbers;
    std::optional<std::string> climax_scene_id;
};

struct Chapter {
    std::string id, title, pitch, notes, content;
    int number = 0;
    std::optional<std::string> arc_id;
    ChapterStatus status = ChapterStatus::Outline;
    nlohmann::json emotional_curve = nlohmann::json::array();
    std::vector<std::string> scene_ids, foreshadowing_planted, foreshadowing_paid;
};

struct Section {
    std::string id, chapter_id, title;
    int order = 0;
    std::vector<std::string> scene_ids;
};

struct Scene {
    std::string id, title, chapter_id, world_time, narrative;
    std::optional<std::string> section_id, location_id;
    std::vector<std::string> participant_ids;
    SceneStatus status = SceneStatus::Draft;
};

struct TimelineEvent {
    std::string id, world_time, description, recorded_by;
    std::vector<std::string> affected_character_ids, related_scene_ids;
};

struct Foreshadowing {
    std::string id, content, pay_off_idea;
    std::optional<std::string> planted_at, paid_at;
    ForeshadowStatus status = ForeshadowStatus::Open;
    ForeshadowHintLevel hint_level = ForeshadowHintLevel::Visible;
    std::vector<std::string> tags, related_foreshadowing_ids, related_secret_ids;
    ForeshadowCreatedBy created_by = ForeshadowCreatedBy::Author;
};

struct Secret {
    std::string id, holder_id, truth, public_version, stakes;
    std::optional<std::string> planted_at, exposed_at;
    std::vector<std::string> aware_character_ids, suspicious_character_ids,
        related_foreshadowing_ids;
    nlohmann::json believed_truths = nlohmann::json::object();
    SecretStatus status = SecretStatus::Active;
};

struct CharacterCard {
    std::string agent_id, name, gender, race, identity, emotional_tendency,
        speaking_style, core_desire, deep_fear, daily_goal, background,
        knowledge_scope, appearance, updated_at;
    int age = 0;
    int version = 1;
    std::vector<std::string> core_traits, taboo_topics;
    nlohmann::json relations = nlohmann::json::object();
};

struct DiaryEntry {
    std::string id, agent_id, scene_id, world_time, content, created_at;
};

struct MemorySummary {
    std::string id, agent_id, period_start, period_end, summary, created_at;
    std::vector<std::string> source_diary_ids;
};

struct RelationEntry {
    std::string agent_id, target_id, relation_type, description, updated_at;
    int intimacy = 0;
    std::vector<std::string> key_events;
};

struct VoiceFingerprint {
    std::string agent_id, updated_at;
    double avg_sentence_length = 0.0, sentence_variance = 0.0,
           question_frequency = 0.0, modifier_ratio = 0.0;
    int sample_count = 0;
    std::vector<std::string> signature_words;
    nlohmann::json tone_profile = nlohmann::json::object();
};

struct VoiceComparison {
    std::string left_agent_id, right_agent_id;
    double similarity = 0.0;
    std::vector<std::string> shared_features, differences, suggestions;
};

struct StoryStructure {
    NarrativeTemplate template_type = NarrativeTemplate::Freeform;
    std::string name;
    std::vector<std::string> stages;
};

inline std::string to_string(AgentKind value) {
    switch (value) {
    case AgentKind::God:
        return "god";
    case AgentKind::MapManager:
        return "map_manager";
    case AgentKind::HistoryManager:
        return "history_manager";
    case AgentKind::MagicSystemManager:
        return "magic_system_manager";
    case AgentKind::FactionManager:
        return "faction_manager";
    case AgentKind::RelationManager:
        return "relation_manager";
    case AgentKind::Individual:
        return "individual";
    case AgentKind::Group:
        return "group";
    }
    return "individual";
}

inline std::string to_string(ChapterStatus value) {
    switch (value) {
    case ChapterStatus::Outline:
        return "outline";
    case ChapterStatus::Drafting:
        return "drafting";
    case ChapterStatus::Completed:
        return "completed";
    case ChapterStatus::Revised:
        return "revised";
    }
    return "outline";
}

inline std::string to_string(SceneStatus value) {
    switch (value) {
    case SceneStatus::Draft:
        return "draft";
    case SceneStatus::Writing:
        return "writing";
    case SceneStatus::Completed:
        return "completed";
    }
    return "draft";
}

inline std::string to_string(ForeshadowStatus value) {
    switch (value) {
    case ForeshadowStatus::Open:
        return "open";
    case ForeshadowStatus::Paid:
        return "paid";
    case ForeshadowStatus::Abandoned:
        return "abandoned";
    }
    return "open";
}

inline std::string to_string(ForeshadowHintLevel value) {
    switch (value) {
    case ForeshadowHintLevel::Subtle:
        return "subtle";
    case ForeshadowHintLevel::Visible:
        return "visible";
    case ForeshadowHintLevel::Obvious:
        return "obvious";
    }
    return "visible";
}

inline std::string to_string(ForeshadowCreatedBy value) {
    switch (value) {
    case ForeshadowCreatedBy::Author:
        return "author";
    case ForeshadowCreatedBy::GodAgentDetected:
        return "god_agent_detected";
    }
    return "author";
}

inline std::string to_string(SecretStatus value) {
    switch (value) {
    case SecretStatus::Active:
        return "active";
    case SecretStatus::Exposed:
        return "exposed";
    case SecretStatus::Abandoned:
        return "abandoned";
    }
    return "active";
}

inline std::string to_string(NarrativeTemplate value) {
    switch (value) {
    case NarrativeTemplate::ThreeAct:
        return "three_act";
    case NarrativeTemplate::FourAct:
        return "four_act";
    case NarrativeTemplate::HerosJourney:
        return "heros_journey";
    case NarrativeTemplate::Freeform:
        return "freeform";
    }
    return "freeform";
}

inline std::string to_string(KnowledgeState value) {
    switch (value) {
    case KnowledgeState::Public:
        return "public";
    case KnowledgeState::Secret:
        return "secret";
    case KnowledgeState::Unknown:
        return "unknown";
    }
    return "unknown";
}

// ─── Knowledge Graph Extraction types ───

enum class CandidateStatus {
    New,
    Conflict,
    StanceChange,
    FactUpdate,
    NoChange
};

struct ExtractionCandidate {
    merak::kg::GraphRelation relation;
    CandidateStatus status;
    std::optional<merak::kg::GraphRelation> existing;
    std::string evidence;
    std::vector<std::string> change_summary;
};

struct ExtractionResult {
    std::vector<ExtractionCandidate> candidates;
    std::string scene_id;
    std::string world_id;
    std::string extraction_timestamp;
};

enum class ExtractionConfirmAction { Approve, Reject, Edit, KeepExisting };

} // namespace merak::worldbuilding
