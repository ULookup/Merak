#pragma once

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
};

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
    std::string id, title, pitch, notes;
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

} // namespace merak::worldbuilding
