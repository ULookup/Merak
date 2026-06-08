#include <merak/dsl/resolver.hpp>

#include <merak/worldbuilding/agent_store.hpp>
#include <merak/worldbuilding/foreshadowing_store.hpp>
#include <merak/worldbuilding/narrative_store.hpp>
#include <merak/worldbuilding/secret_store.hpp>
#include <merak/worldbuilding/world_models.hpp>
#include <merak/worldbuilding/world_store.hpp>

#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace merak::dsl {

using namespace worldbuilding;

namespace {

// Helper: find an agent by name in a list of AgentRecords
std::optional<AgentRecord> find_agent_by_name(const std::vector<AgentRecord>& agents,
                                               const std::string& name) {
    for (const auto& a : agents) {
        if (a.name == name || a.display_name == name) {
            return a;
        }
    }
    return std::nullopt;
}

// Helper: convert ForeshadowStatus string to enum
std::optional<ForeshadowStatus> parse_foreshadow_status(const std::string& s) {
    if (s == "open") return ForeshadowStatus::Open;
    if (s == "paid") return ForeshadowStatus::Paid;
    if (s == "abandoned") return ForeshadowStatus::Abandoned;
    return std::nullopt;
}

// Helper: convert SecretStatus string to enum
std::optional<SecretStatus> parse_secret_status(const std::string& s) {
    if (s == "active") return SecretStatus::Active;
    if (s == "exposed") return SecretStatus::Exposed;
    if (s == "abandoned") return SecretStatus::Abandoned;
    return std::nullopt;
}

// Format a CharacterCard as Markdown
std::string format_character_card(const CharacterCard& card) {
    std::ostringstream os;
    os << "### Character: " << card.name << "\n\n";

    if (!card.identity.empty())
        os << "**Identity:** " << card.identity << "\n";
    os << "**Age:** " << card.age;
    if (!card.gender.empty()) os << " | **Gender:** " << card.gender;
    if (!card.race.empty()) os << " | **Race:** " << card.race;
    os << "\n\n";

    if (!card.core_traits.empty()) {
        os << "**Traits:** ";
        for (size_t i = 0; i < card.core_traits.size(); ++i) {
            if (i > 0) os << ", ";
            os << card.core_traits[i];
        }
        os << "\n\n";
    }

    if (!card.emotional_tendency.empty())
        os << "**Emotional Tendency:** " << card.emotional_tendency << "\n";
    if (!card.speaking_style.empty())
        os << "**Speaking Style:** " << card.speaking_style << "\n";
    if (!card.core_desire.empty())
        os << "**Core Desire:** " << card.core_desire << "\n";
    if (!card.deep_fear.empty())
        os << "**Deep Fear:** " << card.deep_fear << "\n";
    if (!card.daily_goal.empty())
        os << "**Daily Goal:** " << card.daily_goal << "\n\n";

    if (!card.background.empty())
        os << "**Background:** " << card.background << "\n\n";
    if (!card.knowledge_scope.empty())
        os << "**Knowledge Scope:** " << card.knowledge_scope << "\n\n";
    if (!card.appearance.empty())
        os << "**Appearance:** " << card.appearance << "\n\n";

    if (!card.taboo_topics.empty()) {
        os << "**Taboo Topics:** ";
        for (size_t i = 0; i < card.taboo_topics.size(); ++i) {
            if (i > 0) os << ", ";
            os << card.taboo_topics[i];
        }
        os << "\n\n";
    }

    return os.str();
}

// Format a Scene as Markdown (full Scene struct from get_scene)
std::string format_scene_info(const Scene& scene) {
    std::ostringstream os;
    os << "### Scene: " << scene.title << "\n\n";
    os << "**ID:** " << scene.id << "\n";
    os << "**Chapter:** " << scene.chapter_id << "\n";
    os << "**World Time:** " << scene.world_time << "\n";
    os << "**Status:** " << to_string(scene.status) << "\n";
    if (!scene.participant_ids.empty()) {
        os << "**Participants:** ";
        for (size_t i = 0; i < scene.participant_ids.size(); ++i) {
            if (i > 0) os << ", ";
            os << scene.participant_ids[i];
        }
        os << "\n";
    }
    return os.str();
}

// Format chapter summary as Markdown
std::string format_chapter_info(const ChapterSummary& ch) {
    std::ostringstream os;
    os << "### Chapter: " << ch.title << "\n\n";
    os << "**ID:** " << ch.id << "\n";
    os << "**Number:** " << ch.number << "\n";
    os << "**Status:** " << ch.status << "\n";
    os << "**Scene Count:** " << ch.scene_count << "\n";
    if (ch.arc_id.has_value())
        os << "**Arc:** " << ch.arc_id.value() << "\n";
    return os.str();
}

// Format arc summary as Markdown
std::string format_arc_info(const ArcSummary& arc) {
    std::ostringstream os;
    os << "### Arc: " << arc.title << "\n\n";
    os << "**ID:** " << arc.id << "\n";
    os << "**Purpose:** " << arc.purpose << "\n";
    os << "**Status:** " << arc.status << "\n";
    return os.str();
}

// Format foreshadowing item as Markdown
std::string format_foreshadowing(const Foreshadowing& f) {
    std::ostringstream os;
    os << "**[" << f.id << "]** " << f.content << "\n";
    if (!f.pay_off_idea.empty())
        os << "  - Pay-off idea: " << f.pay_off_idea << "\n";
    os << "  - Status: " << to_string(f.status)
       << " | Hint: " << to_string(f.hint_level) << "\n";
    if (!f.tags.empty()) {
        os << "  - Tags: ";
        for (size_t i = 0; i < f.tags.size(); ++i) {
            if (i > 0) os << ", ";
            os << f.tags[i];
        }
        os << "\n";
    }
    if (f.planted_at.has_value())
        os << "  - Planted at: " << f.planted_at.value() << "\n";
    if (f.paid_at.has_value())
        os << "  - Paid at: " << f.paid_at.value() << "\n";
    return os.str();
}

// Format secret as Markdown
std::string format_secret(const Secret& s) {
    std::ostringstream os;
    os << "**[" << s.id << "]** Truth: " << s.truth << "\n";
    if (!s.public_version.empty())
        os << "  - Public version: " << s.public_version << "\n";
    os << "  - Holder: " << s.holder_id
       << " | Status: " << to_string(s.status) << "\n";
    if (!s.stakes.empty())
        os << "  - Stakes: " << s.stakes << "\n";
    if (!s.aware_character_ids.empty()) {
        os << "  - Aware: ";
        for (size_t i = 0; i < s.aware_character_ids.size(); ++i) {
            if (i > 0) os << ", ";
            os << s.aware_character_ids[i];
        }
        os << "\n";
    }
    return os.str();
}

// Format diary entries as Markdown
std::string format_diary_entries(const std::vector<DiaryEntry>& entries) {
    std::ostringstream os;
    if (entries.empty()) {
        os << "*No diary entries found.*\n";
        return os.str();
    }
    for (const auto& e : entries) {
        os << "**[" << e.world_time << "]** " << e.content << "\n";
    }
    return os.str();
}

// Format relations as Markdown
std::string format_relations(const std::vector<RelationEntry>& relations) {
    std::ostringstream os;
    if (relations.empty()) {
        os << "*No relations found.*\n";
        return os.str();
    }
    for (const auto& r : relations) {
        os << "- **" << r.target_id << "** (" << r.relation_type
           << ", intimacy: " << r.intimacy << ")\n";
        if (!r.description.empty())
            os << "  " << r.description << "\n";
        if (!r.key_events.empty()) {
            os << "  Key events: ";
            for (size_t i = 0; i < r.key_events.size(); ++i) {
                if (i > 0) os << ", ";
                os << r.key_events[i];
            }
            os << "\n";
        }
    }
    return os.str();
}

} // anonymous namespace

ResolvedContent Resolver::resolve(const DslRef& ref) {
    ResolvedContent result;
    result.ref_raw = ref.raw;

    std::ostringstream os;

    // Helper: get param value or empty
    auto param = [&](const std::string& key) -> std::string {
        auto it = ref.params.find(key);
        return (it != ref.params.end()) ? it->second : std::string{};
    };

    if (ref.type == "agent") {
        std::string agent_id;
        if (auto name = param("name"); !name.empty()) {
            auto agents = svc_.agents().list_agents(world_id_);
            auto found = find_agent_by_name(agents, name);
            if (found.has_value()) {
                agent_id = found->id;
            } else {
                result.rendered = "<!-- DSL: agent '" + name + "' not found in world " + world_id_ + " -->";
                return result;
            }
        } else if (auto id = param("id"); !id.empty()) {
            agent_id = id;
        } else {
            agent_id = agent_id_;
        }

        if (agent_id.empty()) {
            result.rendered = "<!-- DSL: no agent specified -->";
            return result;
        }

        auto card = svc_.agents().load_character_card(agent_id);
        result.rendered = format_character_card(card);
        return result;
    }

    if (ref.type == "scene") {
        // Resolve scene id: explicit 'id' takes precedence, then 'current' flag
        std::string scene_id;
        if (auto id = param("id"); !id.empty()) {
            scene_id = id;
        } else if (ref.params.count("current") > 0) {
            scene_id = scene_id_;
        }

        if (scene_id.empty()) {
            result.rendered = "<!-- DSL: no scene context available -->";
            return result;
        }

        auto scene = svc_.narrative().get_scene(world_id_, scene_id);
        if (scene.has_value()) {
            result.rendered = format_scene_info(scene.value());
        } else {
            result.rendered = "<!-- DSL: scene '" + scene_id + "' not found -->";
        }
        return result;
    }

    if (ref.type == "chapter") {
        std::string chapter_id;
        if (auto id = param("id"); !id.empty()) {
            chapter_id = id;
        } else if (ref.params.count("current") > 0) {
            chapter_id = chapter_id_;
        }

        if (chapter_id.empty()) {
            result.rendered = "<!-- DSL: no chapter context available -->";
            return result;
        }

        auto chapters = svc_.narrative().list_chapters(world_id_);
        for (const auto& ch : chapters) {
            if (ch.id == chapter_id) {
                result.rendered = format_chapter_info(ch);
                return result;
            }
        }
        result.rendered = "<!-- DSL: chapter '" + chapter_id + "' not found -->";
        return result;
    }

    if (ref.type == "arc") {
        std::string arc_id;
        if (auto id = param("id"); !id.empty()) {
            arc_id = id;
        } else if (ref.params.count("current") > 0) {
            arc_id = arc_id_;
        }

        if (arc_id.empty()) {
            result.rendered = "<!-- DSL: no arc context available -->";
            return result;
        }

        auto arcs = svc_.narrative().list_arcs(world_id_);
        for (const auto& a : arcs) {
            if (a.id == arc_id) {
                result.rendered = format_arc_info(a);
                return result;
            }
        }
        result.rendered = "<!-- DSL: arc '" + arc_id + "' not found -->";
        return result;
    }

    if (ref.type == "foreshadow") {
        // Check for specific id
        if (auto id = param("id"); !id.empty()) {
            auto items = svc_.foreshadowing().list(world_id_, std::nullopt);
            for (const auto& f : items) {
                if (f.id == id) {
                    result.rendered = "### Foreshadowing\n\n" + format_foreshadowing(f) + "\n";
                    return result;
                }
            }
            result.rendered = "<!-- DSL: foreshadowing '" + id + "' not found -->";
            return result;
        }

        // Filter by status
        std::optional<ForeshadowStatus> status_filter;
        if (auto status_str = param("status"); !status_str.empty()) {
            status_filter = parse_foreshadow_status(status_str);
        }

        auto items = svc_.foreshadowing().list(world_id_, status_filter);

        os << "### Foreshadowing\n\n";
        if (items.empty()) {
            os << "*No foreshadowing items found.*\n";
        } else {
            for (const auto& f : items) {
                os << format_foreshadowing(f) << "\n";
            }
        }
        result.rendered = os.str();
        return result;
    }

    if (ref.type == "secret") {
        // Check for specific id
        if (auto id = param("id"); !id.empty()) {
            auto items = svc_.secrets().list(world_id_, std::nullopt);
            for (const auto& s : items) {
                if (s.id == id) {
                    result.rendered = "### Secrets\n\n" + format_secret(s) + "\n";
                    return result;
                }
            }
            result.rendered = "<!-- DSL: secret '" + id + "' not found -->";
            return result;
        }

        // Filter by status
        std::optional<SecretStatus> status_filter;
        if (auto status_str = param("status"); !status_str.empty()) {
            status_filter = parse_secret_status(status_str);
        }

        auto items = svc_.secrets().list(world_id_, status_filter);

        os << "### Secrets\n\n";
        if (items.empty()) {
            os << "*No secrets found.*\n";
        } else {
            for (const auto& s : items) {
                os << format_secret(s) << "\n";
            }
        }
        result.rendered = os.str();
        return result;
    }

    if (ref.type == "world") {
        auto world = svc_.worlds().get_world(world_id_);
        if (!world.has_value()) {
            result.rendered = "<!-- DSL: world '" + world_id_ + "' not found -->";
            return result;
        }

        auto agents = svc_.agents().list_agents(world_id_);
        auto chapters = svc_.narrative().list_chapters(world_id_);
        auto arcs = svc_.narrative().list_arcs(world_id_);

        os << "### World: " << world->name << "\n\n";
        os << "**Description:** " << world->description << "\n\n";
        os << "**Stats:** " << agents.size() << " agents, "
           << chapters.size() << " chapters, "
           << arcs.size() << " arcs\n";
        os << "**Created:** " << world->created_at << "\n";

        result.rendered = os.str();
        return result;
    }

    if (ref.type == "diary") {
        std::string target_agent;
        if (auto agent_param = param("agent"); !agent_param.empty()) {
            target_agent = agent_param;
        } else {
            target_agent = agent_id_;
        }

        if (target_agent.empty()) {
            result.rendered = "<!-- DSL: no agent specified for diary -->";
            return result;
        }

        int limit = 5;
        if (auto limit_str = param("limit"); !limit_str.empty()) {
            try {
                limit = std::stoi(limit_str);
            } catch (...) {
                limit = 5;
            }
        }

        auto entries = svc_.agents().recent_diary(target_agent, limit);

        os << "### Recent Diary Entries\n\n";
        os << format_diary_entries(entries);
        result.rendered = os.str();
        return result;
    }

    if (ref.type == "relation") {
        std::string target_agent;
        if (auto agent_param = param("agent"); !agent_param.empty()) {
            target_agent = agent_param;
        } else {
            target_agent = agent_id_;
        }

        if (target_agent.empty()) {
            result.rendered = "<!-- DSL: no agent specified for relations -->";
            return result;
        }

        auto relations = svc_.agents().relations_for(target_agent);

        os << "### Relations\n\n";
        os << format_relations(relations);
        result.rendered = os.str();
        return result;
    }

    // Unknown type
    result.rendered = "<!-- DSL: unknown type '" + ref.type + "' -->";
    return result;
}

} // namespace merak::dsl
