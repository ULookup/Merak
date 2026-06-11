#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace merak::kg {

enum class EntityType { Agent, Location, Organization, Item, Concept };

struct GraphEntity {
    std::string name;
    EntityType type;
    std::string source_id;
    std::string world_id;
    std::string created_at;
};

enum class RelationKind {
    Acquaintance,
    Friend,
    Lover,
    Kin,
    MasterApprentice,
    SuperiorSubordinate,
    Enemy,
    Rival,
    Ally,
    Member,
    Owner,
    Guardian,
    Benefactor,
    Grudge,
    Custom
};

enum class Stance {
    Friendly, Admiring, Dependent,
    Neutral,
    Cautious, Distant, Suspicious,
    Hostile, Resentful, Fearful,
    Guilty, Disdainful,
    Unknown
};

struct GraphRelation {
    std::string source_id;
    std::string target_id;
    std::string source_name;
    std::string target_name;
    EntityType source_type;
    EntityType target_type;
    std::string world_id;

    RelationKind kind = RelationKind::Acquaintance;
    std::string kind_en;
    std::string kind_cn;
    std::string kind_custom;

    Stance a_to_b_stance = Stance::Neutral;
    Stance b_to_a_stance = Stance::Neutral;
    std::string a_to_b_addressing;
    std::string b_to_a_addressing;

    std::string fact;
    std::string description;

    std::string created_at;
    std::string updated_at;
};

struct RelationKey {
    std::string source_id;
    std::string target_id;
    std::string kind_en;
    std::string world_id;
};

struct QueryFilters {
    std::optional<std::vector<std::string>> kind_filter;
    std::optional<std::vector<Stance>> stance_filter;
    std::optional<std::pair<int, int>> chapter_range;
    int top_k = 50;
};

struct SubGraph {
    std::vector<GraphRelation> relations;
    std::vector<std::string> fact_summaries;
};

struct NeighborGraph {
    std::string center_entity;
    std::vector<GraphRelation> relations;
    std::vector<GraphEntity> neighbor_entities;
};

struct PathResult {
    bool found;
    std::vector<std::vector<GraphRelation>> paths;
};

// ─── Enum ↔ string conversions ───

inline std::string to_string(EntityType t) {
    switch (t) {
    case EntityType::Agent:         return "Agent";
    case EntityType::Location:      return "Location";
    case EntityType::Organization:  return "Organization";
    case EntityType::Item:          return "Item";
    case EntityType::Concept:       return "Concept";
    }
    return "Agent";
}

inline EntityType entity_type_from_string(const std::string& s) {
    if (s == "Location")      return EntityType::Location;
    if (s == "Organization")  return EntityType::Organization;
    if (s == "Item")          return EntityType::Item;
    if (s == "Concept")       return EntityType::Concept;
    return EntityType::Agent;
}

inline std::string to_string(RelationKind k) {
    switch (k) {
    case RelationKind::Acquaintance:        return "acquaintance";
    case RelationKind::Friend:              return "friend";
    case RelationKind::Lover:               return "lover";
    case RelationKind::Kin:                 return "kin";
    case RelationKind::MasterApprentice:    return "master_apprentice";
    case RelationKind::SuperiorSubordinate: return "superior_subordinate";
    case RelationKind::Enemy:               return "enemy";
    case RelationKind::Rival:               return "rival";
    case RelationKind::Ally:                return "ally";
    case RelationKind::Member:              return "member";
    case RelationKind::Owner:               return "owner";
    case RelationKind::Guardian:            return "guardian";
    case RelationKind::Benefactor:          return "benefactor";
    case RelationKind::Grudge:              return "grudge";
    case RelationKind::Custom:              return "custom";
    }
    return "custom";
}

inline std::string relation_kind_cn(RelationKind k) {
    switch (k) {
    case RelationKind::Acquaintance:        return "认识";
    case RelationKind::Friend:              return "朋友";
    case RelationKind::Lover:               return "恋人";
    case RelationKind::Kin:                 return "血缘";
    case RelationKind::MasterApprentice:    return "师徒";
    case RelationKind::SuperiorSubordinate: return "上下级";
    case RelationKind::Enemy:               return "敌对";
    case RelationKind::Rival:               return "竞争";
    case RelationKind::Ally:                return "合作/盟友";
    case RelationKind::Member:              return "隶属";
    case RelationKind::Owner:               return "拥有";
    case RelationKind::Guardian:            return "守护";
    case RelationKind::Benefactor:          return "恩人";
    case RelationKind::Grudge:              return "仇人";
    case RelationKind::Custom:              return "自定义";
    }
    return "自定义";
}

inline std::string to_string(Stance s) {
    switch (s) {
    case Stance::Friendly:   return "Friendly";
    case Stance::Admiring:   return "Admiring";
    case Stance::Dependent:  return "Dependent";
    case Stance::Neutral:    return "Neutral";
    case Stance::Cautious:   return "Cautious";
    case Stance::Distant:    return "Distant";
    case Stance::Suspicious: return "Suspicious";
    case Stance::Hostile:    return "Hostile";
    case Stance::Resentful:  return "Resentful";
    case Stance::Fearful:    return "Fearful";
    case Stance::Guilty:     return "Guilty";
    case Stance::Disdainful: return "Disdainful";
    case Stance::Unknown:    return "Unknown";
    }
    return "Unknown";
}

inline RelationKind relation_kind_from_string(const std::string& s) {
    if (s == "acquaintance")         return RelationKind::Acquaintance;
    if (s == "friend")               return RelationKind::Friend;
    if (s == "lover")                return RelationKind::Lover;
    if (s == "kin")                  return RelationKind::Kin;
    if (s == "master_apprentice")    return RelationKind::MasterApprentice;
    if (s == "superior_subordinate") return RelationKind::SuperiorSubordinate;
    if (s == "enemy")                return RelationKind::Enemy;
    if (s == "rival")                return RelationKind::Rival;
    if (s == "ally")                 return RelationKind::Ally;
    if (s == "member")               return RelationKind::Member;
    if (s == "owner")                return RelationKind::Owner;
    if (s == "guardian")             return RelationKind::Guardian;
    if (s == "benefactor")           return RelationKind::Benefactor;
    if (s == "grudge")               return RelationKind::Grudge;
    return RelationKind::Custom;
}

inline Stance stance_from_string(const std::string& s) {
    if (s == "Friendly")   return Stance::Friendly;
    if (s == "Admiring")   return Stance::Admiring;
    if (s == "Dependent")  return Stance::Dependent;
    if (s == "Neutral")    return Stance::Neutral;
    if (s == "Cautious")   return Stance::Cautious;
    if (s == "Distant")    return Stance::Distant;
    if (s == "Suspicious") return Stance::Suspicious;
    if (s == "Hostile")    return Stance::Hostile;
    if (s == "Resentful")  return Stance::Resentful;
    if (s == "Fearful")    return Stance::Fearful;
    if (s == "Guilty")     return Stance::Guilty;
    if (s == "Disdainful") return Stance::Disdainful;
    return Stance::Unknown;
}

// ─── JSON serialization ───

inline void to_json(nlohmann::json& j, const GraphEntity& e) {
    j = {{"name", e.name}, {"type", to_string(e.type)},
         {"source_id", e.source_id}, {"world_id", e.world_id},
         {"created_at", e.created_at}};
}

inline void from_json(const nlohmann::json& j, GraphEntity& e) {
    j.at("name").get_to(e.name);
    if (j.contains("type")) e.type = entity_type_from_string(j.at("type").get<std::string>());
    j.at("source_id").get_to(e.source_id);
    j.at("world_id").get_to(e.world_id);
    j.at("created_at").get_to(e.created_at);
}

inline void to_json(nlohmann::json& j, const GraphRelation& r) {
    j = {{"source_id", r.source_id}, {"target_id", r.target_id},
         {"source_name", r.source_name}, {"target_name", r.target_name},
         {"source_type", to_string(r.source_type)},
         {"target_type", to_string(r.target_type)},
         {"world_id", r.world_id},
         {"kind_en", r.kind_en}, {"kind_cn", r.kind_cn},
         {"kind_custom", r.kind_custom},
         {"a_to_b_stance", to_string(r.a_to_b_stance)},
         {"b_to_a_stance", to_string(r.b_to_a_stance)},
         {"a_to_b_addressing", r.a_to_b_addressing},
         {"b_to_a_addressing", r.b_to_a_addressing},
         {"fact", r.fact}, {"description", r.description},
         {"created_at", r.created_at}, {"updated_at", r.updated_at}};
}

inline void from_json(const nlohmann::json& j, GraphRelation& r) {
    j.at("source_id").get_to(r.source_id);
    j.at("target_id").get_to(r.target_id);
    j.at("source_name").get_to(r.source_name);
    j.at("target_name").get_to(r.target_name);
    if (j.contains("source_type")) r.source_type = entity_type_from_string(j.at("source_type").get<std::string>());
    if (j.contains("target_type")) r.target_type = entity_type_from_string(j.at("target_type").get<std::string>());
    if (j.contains("kind_en")) {
        j.at("kind_en").get_to(r.kind_en);
        r.kind = relation_kind_from_string(r.kind_en);
    }
    if (j.contains("kind_cn")) j.at("kind_cn").get_to(r.kind_cn);
    if (j.contains("kind_custom")) j.at("kind_custom").get_to(r.kind_custom);
    if (j.contains("a_to_b_stance")) r.a_to_b_stance = stance_from_string(j.at("a_to_b_stance").get<std::string>());
    if (j.contains("b_to_a_stance")) r.b_to_a_stance = stance_from_string(j.at("b_to_a_stance").get<std::string>());
    if (j.contains("a_to_b_addressing")) j.at("a_to_b_addressing").get_to(r.a_to_b_addressing);
    if (j.contains("b_to_a_addressing")) j.at("b_to_a_addressing").get_to(r.b_to_a_addressing);
    if (j.contains("fact")) j.at("fact").get_to(r.fact);
    if (j.contains("description")) j.at("description").get_to(r.description);
    if (j.contains("world_id")) j.at("world_id").get_to(r.world_id);
    if (j.contains("created_at")) j.at("created_at").get_to(r.created_at);
    if (j.contains("updated_at")) j.at("updated_at").get_to(r.updated_at);
}

} // namespace merak::kg
