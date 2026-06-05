#include <merak/worldbuilding/agent_store.hpp>

#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/pg_helpers.hpp>

#include <algorithm>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace merak::worldbuilding {

AgentStore::~AgentStore() = default;

namespace {

AgentKind agent_kind_from_string(const std::string& value) {
    if (value == "god") {
        return AgentKind::God;
    }
    if (value == "map_manager") {
        return AgentKind::MapManager;
    }
    if (value == "history_manager") {
        return AgentKind::HistoryManager;
    }
    if (value == "magic_system_manager") {
        return AgentKind::MagicSystemManager;
    }
    if (value == "faction_manager") {
        return AgentKind::FactionManager;
    }
    if (value == "individual") {
        return AgentKind::Individual;
    }
    if (value == "group") {
        return AgentKind::Group;
    }
    throw std::runtime_error("unknown agent kind: " + value);
}

std::string to_pg_array(const std::vector<std::string>& values) {
    if (values.empty()) return "{}";
    std::ostringstream out;
    out << '{';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out << ',';
        out << '"';
        for (char c : values[i]) {
            if (c == '"') out << '"'; // escape embedded double quote
            out << c;
        }
        out << '"';
    }
    out << '}';
    return out.str();
}

AgentRecord read_agent_record_pg(const PgResult& res, int row) {
    return AgentRecord{
        .id = res.get(row, 0),
        .world_id = res.get(row, 1),
        .name = res.get(row, 2),
        .display_name = res.get(row, 3),
        .created_at = res.get(row, 5),
        .updated_at = res.get(row, 6),
        .kind = agent_kind_from_string(res.get(row, 4)),
    };
}

void write_text(const std::filesystem::path& path,
                const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to write " + path.string());
    }
    output << content;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to read " + path.string());
    }
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

void remove_all_no_throw(const std::filesystem::path& path) noexcept {
    try {
        std::filesystem::remove_all(path);
    } catch (...) {
    }
}

std::string join_zh(const std::vector<std::string>& values) {
    std::ostringstream output;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            output << "、";
        }
        output << values[i];
    }
    return output.str();
}

nlohmann::json character_card_json(const CharacterCard& card) {
    return nlohmann::json{
        {"agent_id", card.agent_id},
        {"name", card.name},
        {"age", card.age},
        {"gender", card.gender},
        {"race", card.race},
        {"identity", card.identity},
        {"core_traits", card.core_traits},
        {"emotional_tendency", card.emotional_tendency},
        {"speaking_style", card.speaking_style},
        {"taboo_topics", card.taboo_topics},
        {"core_desire", card.core_desire},
        {"deep_fear", card.deep_fear},
        {"daily_goal", card.daily_goal},
        {"background", card.background},
        {"knowledge_scope", card.knowledge_scope},
        {"relations", card.relations},
        {"appearance", card.appearance},
        {"version", card.version},
        {"updated_at", card.updated_at},
    };
}

CharacterCard character_card_from_json(const nlohmann::json& json) {
    CharacterCard card;
    card.agent_id = json.at("agent_id").get<std::string>();
    card.name = json.at("name").get<std::string>();
    card.age = json.at("age").get<int>();
    card.gender = json.at("gender").get<std::string>();
    card.race = json.at("race").get<std::string>();
    card.identity = json.at("identity").get<std::string>();
    card.core_traits = json.at("core_traits").get<std::vector<std::string>>();
    card.emotional_tendency =
        json.at("emotional_tendency").get<std::string>();
    card.speaking_style = json.at("speaking_style").get<std::string>();
    card.taboo_topics = json.at("taboo_topics").get<std::vector<std::string>>();
    card.core_desire = json.at("core_desire").get<std::string>();
    card.deep_fear = json.at("deep_fear").get<std::string>();
    card.daily_goal = json.at("daily_goal").get<std::string>();
    card.background = json.at("background").get<std::string>();
    card.knowledge_scope = json.at("knowledge_scope").get<std::string>();
    card.relations = json.at("relations");
    card.appearance = json.at("appearance").get<std::string>();
    card.version = json.at("version").get<int>();
    card.updated_at = json.at("updated_at").get<std::string>();
    return card;
}

nlohmann::json extract_character_card_json(const std::string& markdown) {
    constexpr std::string_view kBegin = "```merak-character-card-json\n";
    constexpr std::string_view kEnd = "\n```";
    const auto begin = markdown.find(kBegin);
    if (begin == std::string::npos) {
        throw std::runtime_error("character card is missing structured data");
    }
    const auto json_start = begin + kBegin.size();
    const auto end = markdown.find(kEnd, json_start);
    if (end == std::string::npos) {
        throw std::runtime_error("character card structured data is malformed");
    }
    return nlohmann::json::parse(markdown.substr(json_start, end - json_start));
}

std::string character_card_markdown(const CharacterCard& card) {
    std::ostringstream output;
    output << "# " << card.name << "\n\n";
    output << "Agent ID：" << card.agent_id << "\n";
    output << "版本：" << card.version << "\n";
    output << "更新时间：" << card.updated_at << "\n";
    output << "姓名：" << card.name << "\n";
    output << "年龄：" << card.age << "\n";
    output << "性别：" << card.gender << "\n";
    output << "种族：" << card.race << "\n";
    output << "身份：" << card.identity << "\n";
    output << "核心性格特质：" << join_zh(card.core_traits) << "\n";
    output << "情绪倾向：" << card.emotional_tendency << "\n";
    output << "说话风格：" << card.speaking_style << "\n";
    output << "禁忌话题：" << join_zh(card.taboo_topics) << "\n";
    output << "核心欲望：" << card.core_desire << "\n";
    output << "深层恐惧：" << card.deep_fear << "\n";
    output << "日常目标：" << card.daily_goal << "\n";
    output << "背景故事：" << card.background << "\n";
    output << "知识范围：" << card.knowledge_scope << "\n";
    output << "人际关系：" << card.relations.dump() << "\n";
    output << "外貌与习惯：" << card.appearance << "\n";
    output << "\n```merak-character-card-json\n";
    output << character_card_json(card).dump(2) << "\n";
    output << "```\n";
    return output.str();
}

CharacterCard parse_character_card_markdown(const std::string& markdown) {
    return character_card_from_json(extract_character_card_json(markdown));
}

std::string history_filename(const std::string& timestamp, int version) {
    return timestamp + "-v" + std::to_string(version) + ".md";
}

std::filesystem::path manager_domain_path(AgentKind kind) {
    switch (kind) {
    case AgentKind::MapManager:
        return "map";
    case AgentKind::HistoryManager:
        return "history";
    case AgentKind::MagicSystemManager:
        return "magic";
    case AgentKind::FactionManager:
        return "faction";
    default:
        throw std::runtime_error("agent kind is not a manager");
    }
}

int clamp_intimacy(int intimacy) {
    return std::clamp(intimacy, -100, 100);
}

void delete_agent_record_no_throw(PgPool& pool,
                                  const std::string& agent_id) noexcept {
    try {
        PgConn conn(pool);
        conn.execute("DELETE FROM agents WHERE id = $1", {agent_id});
    } catch (...) {}
}

struct MemberRefSnapshot {
    std::filesystem::path refs_path;
    std::filesystem::path index_path;
    bool refs_existed = false;
    bool index_existed = false;
    std::string refs_content;
    std::string index_content;
};

void restore_member_refs_no_throw(
    const std::vector<MemberRefSnapshot>& snapshots) noexcept {
    for (const auto& snapshot : snapshots) {
        try {
            if (snapshot.refs_existed) {
                write_text(snapshot.refs_path, snapshot.refs_content);
            } else {
                std::filesystem::remove(snapshot.refs_path);
            }
            if (snapshot.index_existed) {
                write_text(snapshot.index_path, snapshot.index_content);
            } else {
                std::filesystem::remove(snapshot.index_path);
            }
        } catch (...) {
        }
    }
}

} // namespace

AgentStore::AgentStore(WorldStore& worlds, std::string_view pg_conninfo,
                       std::filesystem::path data_root)
    : worlds_(worlds),
      data_root_(std::move(data_root)),
      pool_(std::make_unique<PgPool>(pg_conninfo)) {
    initialize();
}

void AgentStore::initialize() {
    worlds_.initialize();
    PgConn conn(*pool_);
    conn.exec(
        "INSERT INTO agent_metadata(agent_id, can_speak_directly) "
        "SELECT id, CASE WHEN kind = 'group' THEN 0 ELSE 1 END "
        "FROM agents "
        "ON CONFLICT (agent_id) DO NOTHING");
}

std::filesystem::path
AgentStore::agent_path(const std::string& agent_id) const {
    const auto record = get_agent(agent_id);
    if (!record.has_value()) {
        throw std::runtime_error("unknown agent: " + agent_id);
    }
    return worlds_.world_path(record->world_id) / "agents" / agent_id;
}

AgentRecord AgentStore::insert_agent(const std::string& world_id,
                                     std::string name, AgentKind kind) {
    const auto timestamp = now_iso_utc();
    AgentRecord record{
        .id = make_id("agent"),
        .world_id = world_id,
        .name = name,
        .display_name = name,
        .created_at = timestamp,
        .updated_at = timestamp,
        .kind = kind,
    };

    PgConn conn(*pool_);
    conn.exec("BEGIN");
    try {
        conn.execute(
            "INSERT INTO agents(id, world_id, name, display_name, kind, created_at, updated_at) "
            "VALUES($1, $2, $3, $4, $5, $6, $7)",
            {record.id, record.world_id, record.name, record.display_name,
             to_string(record.kind), record.created_at, record.updated_at});

        conn.execute(
            "INSERT INTO agent_metadata(agent_id, can_speak_directly) "
            "VALUES($1, $2)",
            {record.id, std::to_string(kind == AgentKind::Group ? 0 : 1)});

        conn.exec("COMMIT");
    } catch (...) {
        try { conn.exec("ROLLBACK"); } catch (...) {}
        throw;
    }
    return record;
}

AgentRecord AgentStore::create_manager(const std::string& world_id,
                                       AgentKind kind, std::string name,
                                       std::string instructions) {
    const auto domain = manager_domain_path(kind);
    auto record = insert_agent(world_id, std::move(name), kind);

    const auto profile = nlohmann::json{
        {"agent_id", record.id},
        {"domain", domain.string()},
        {"instructions", instructions},
        {"can_speak_directly", true},
    };
    write_text(worlds_.world_path(world_id) / "managers" / domain /
                   "profile.json",
               profile.dump(2));
    return record;
}

AgentRecord AgentStore::create_character(const std::string& world_id,
                                         CharacterCard card) {
    auto record = insert_agent(world_id, card.name, AgentKind::Individual);
    card.agent_id = record.id;
    card.version = 1;
    card.updated_at = record.updated_at;

    const auto root = worlds_.world_path(world_id) / "agents" / record.id;
    std::filesystem::create_directories(root / "character_card_history");
    std::filesystem::create_directories(root / "diary");
    std::filesystem::create_directories(root / "summaries");

    const auto markdown = character_card_markdown(card);
    write_text(root / "character_card.md", markdown);
    write_text(root / "character_card_history" /
                   history_filename(card.updated_at, card.version),
               markdown);
    write_text(root / "memory_index.md",
               "# 记忆索引\n\n## 场景日记\n\n## 记忆摘要\n");
    write_text(root / "relations.md", "# 人际关系\n");

    PgConn conn(*pool_);
    conn.execute(
        "INSERT INTO character_cards(agent_id, name, age, gender, race, identity, "
        "core_traits, emotional_tendency, speaking_style, taboo_topics, "
        "core_desire, deep_fear, daily_goal, background, knowledge_scope, "
        "appearance, version, updated_at) "
        "VALUES($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18)",
        {card.agent_id, card.name,
         std::to_string(card.age), card.gender, card.race, card.identity,
         to_pg_array(card.core_traits), card.emotional_tendency,
         card.speaking_style, to_pg_array(card.taboo_topics),
         card.core_desire, card.deep_fear, card.daily_goal,
         card.background, card.knowledge_scope, card.appearance,
         std::to_string(card.version), card.updated_at});

    return record;
}

AgentRecord AgentStore::create_group(
    const std::string& world_id, std::string name,
    std::string culture_card_markdown,
    std::vector<std::string> member_agent_ids) {
    for (const auto& member_agent_id : member_agent_ids) {
        const auto member = get_agent(member_agent_id);
        if (!member.has_value()) {
            throw std::runtime_error("unknown group member: " +
                                     member_agent_id);
        }
        if (member->world_id != world_id) {
            throw std::runtime_error("group member belongs to another world: " +
                                     member_agent_id);
        }
    }

    auto record = insert_agent(world_id, std::move(name), AgentKind::Group);
    const auto root = worlds_.world_path(world_id) / "agents" / record.id;
    std::vector<MemberRefSnapshot> member_snapshots;

    try {
        std::filesystem::create_directories(root);

        const auto shared_memory_id = "shared_memory:" + record.id;
        const auto shared_memory_ids =
            std::vector<std::string>{shared_memory_id};
        const auto profile = nlohmann::json{
            {"agent_id", record.id},
            {"culture_card_markdown", culture_card_markdown},
            {"member_agent_ids", member_agent_ids},
            {"shared_memory_ids", shared_memory_ids},
            {"can_speak_directly", false},
        };

        write_text(root / "group_profile.json", profile.dump(2));
        write_text(root / "culture_card.md", culture_card_markdown);
        write_text(root / "members.json",
                   nlohmann::json(member_agent_ids).dump(2));
        write_text(root / "shared_memory_refs.json",
                   nlohmann::json(shared_memory_ids).dump(2));

        for (const auto& member_agent_id : member_agent_ids) {
            const auto member_path = agent_path(member_agent_id);
            MemberRefSnapshot snapshot{
                .refs_path = member_path / "group_memory_refs.json",
                .index_path = member_path / "memory_index.md",
                .refs_existed = std::filesystem::exists(member_path /
                                                        "group_memory_refs.json"),
                .index_existed = std::filesystem::exists(member_path /
                                                         "memory_index.md"),
            };
            if (snapshot.refs_existed) {
                snapshot.refs_content = read_text(snapshot.refs_path);
            }
            if (snapshot.index_existed) {
                snapshot.index_content = read_text(snapshot.index_path);
            }
            member_snapshots.push_back(std::move(snapshot));
        }

        for (const auto& snapshot : member_snapshots) {
            auto refs = snapshot.refs_existed ?
                            nlohmann::json::parse(snapshot.refs_content) :
                            nlohmann::json::array();
            refs.push_back({
                {"group_agent_id", record.id},
                {"shared_memory_ids", shared_memory_ids},
            });
            write_text(snapshot.refs_path, refs.dump(2));

            std::ostringstream index;
            index << snapshot.index_content;
            index << "- 群体共享记忆：" << record.id << " -> "
                  << join_zh(shared_memory_ids) << "\n";
            write_text(snapshot.index_path, index.str());
        }
    } catch (...) {
        restore_member_refs_no_throw(member_snapshots);
        remove_all_no_throw(root);
        delete_agent_record_no_throw(*pool_, record.id);
        throw;
    }
    return record;
}

std::optional<AgentRecord>
AgentStore::get_agent(const std::string& agent_id) const {
    PgConn conn(*pool_);
    auto res = conn.query(
        "SELECT id, world_id, name, display_name, kind, created_at, updated_at "
        "FROM agents WHERE id = $1",
        {agent_id});
    if (res.ntuples() == 0) return std::nullopt;
    return read_agent_record_pg(res, 0);
}

std::vector<AgentRecord>
AgentStore::list_agents(const std::string& world_id) const {
    return worlds_.list_agents(world_id);
}

CharacterCard
AgentStore::load_character_card(const std::string& agent_id) const {
    return parse_character_card_markdown(
        read_text(agent_path(agent_id) / "character_card.md"));
}

CharacterCard
AgentStore::update_character_card(const std::string& agent_id,
                                  CharacterCard next_card,
                                  std::string reason) {
    auto record = get_agent(agent_id);
    if (!record.has_value() || record->kind != AgentKind::Individual) {
        throw std::runtime_error("agent is not a character: " + agent_id);
    }

    const auto previous = load_character_card(agent_id);
    next_card.agent_id = agent_id;
    next_card.version = previous.version + 1;
    next_card.updated_at = now_iso_utc();
    const auto markdown = character_card_markdown(next_card);

    const auto root = agent_path(agent_id);
    write_text(root / "character_card.md", markdown);
    write_text(root / "character_card_history" /
                   history_filename(next_card.updated_at, next_card.version),
               markdown + "\n更新原因：" + reason + "\n");

    PgConn conn(*pool_);
    conn.execute(
        "UPDATE agents SET name = $1, display_name = $2, updated_at = $3 "
        "WHERE id = $4",
        {next_card.name, next_card.name, next_card.updated_at, agent_id});

    conn.execute(
        "UPDATE character_cards SET name = $1, age = $2, gender = $3, race = $4, "
        "identity = $5, core_traits = $6, emotional_tendency = $7, "
        "speaking_style = $8, taboo_topics = $9, core_desire = $10, "
        "deep_fear = $11, daily_goal = $12, background = $13, "
        "knowledge_scope = $14, appearance = $15, version = $16, updated_at = $17 "
        "WHERE agent_id = $18",
        {next_card.name, std::to_string(next_card.age),
         next_card.gender, next_card.race, next_card.identity,
         to_pg_array(next_card.core_traits), next_card.emotional_tendency,
         next_card.speaking_style, to_pg_array(next_card.taboo_topics),
         next_card.core_desire, next_card.deep_fear, next_card.daily_goal,
         next_card.background, next_card.knowledge_scope, next_card.appearance,
         std::to_string(next_card.version), next_card.updated_at, agent_id});
    return next_card;
}

void AgentStore::append_diary_entry(DiaryEntry entry) {
    auto record = get_agent(entry.agent_id);
    if (!record.has_value()) {
        throw std::runtime_error("unknown agent: " + entry.agent_id);
    }
    if (entry.id.empty()) {
        entry.id = make_id("diary");
    }
    if (entry.created_at.empty()) {
        entry.created_at = now_iso_utc();
    }

    PgConn conn(*pool_);
    conn.execute(
        "INSERT INTO agent_diaries(id, agent_id, scene_id, world_time, content, created_at) "
        "VALUES($1, $2, $3, $4, $5, $6)",
        {entry.id, entry.agent_id, entry.scene_id, entry.world_time,
         entry.content, entry.created_at});
    // content_tsv updated by trigger

    const auto root = agent_path(entry.agent_id);
    std::ostringstream diary;
    diary << "# 场景日记：" << entry.scene_id << "\n\n";
    diary << "ID：" << entry.id << "\n";
    diary << "世界时间：" << entry.world_time << "\n";
    diary << "创建时间：" << entry.created_at << "\n\n";
    diary << entry.content << "\n";
    write_text(root / "diary" / (entry.id + ".md"), diary.str());

    std::ostringstream index;
    index << read_text(root / "memory_index.md");
    index << "- [" << entry.scene_id << "](diary/" << entry.id
          << ".md) " << entry.world_time << " " << entry.id << "\n";
    write_text(root / "memory_index.md", index.str());
}

std::vector<DiaryEntry>
AgentStore::recent_diary(const std::string& agent_id, int max_entries) const {
    PgConn conn(*pool_);
    auto res = conn.query(
        "SELECT id, agent_id, scene_id, world_time, content, created_at "
        "FROM agent_diaries WHERE agent_id = $1 "
        "ORDER BY created_at DESC, id DESC LIMIT $2",
        {agent_id, std::to_string(std::max(0, max_entries))});

    std::vector<DiaryEntry> entries;
    for (int i = 0; i < res.ntuples(); i++) {
        entries.push_back(DiaryEntry{
            .id = res.get(i, 0),
            .agent_id = res.get(i, 1),
            .scene_id = res.get(i, 2),
            .world_time = res.get(i, 3),
            .content = res.get(i, 4),
            .created_at = res.get(i, 5),
        });
    }
    return entries;
}

void AgentStore::write_memory_summary(MemorySummary summary) {
    auto record = get_agent(summary.agent_id);
    if (!record.has_value()) {
        throw std::runtime_error("unknown agent: " + summary.agent_id);
    }
    if (summary.id.empty()) {
        summary.id = make_id("summary");
    }
    if (summary.created_at.empty()) {
        summary.created_at = now_iso_utc();
    }

    PgConn conn(*pool_);
    conn.execute(
        "INSERT INTO memory_summaries(id, agent_id, period_start, period_end, "
        "summary, source_diary_ids, created_at) "
        "VALUES($1, $2, $3, $4, $5, $6, $7)",
        {summary.id, summary.agent_id, summary.period_start, summary.period_end,
         summary.summary, nlohmann::json(summary.source_diary_ids).dump(),
         summary.created_at});

    const auto root = agent_path(summary.agent_id);
    std::ostringstream markdown;
    markdown << "# 记忆摘要\n\n";
    markdown << "ID：" << summary.id << "\n";
    markdown << "开始：" << summary.period_start << "\n";
    markdown << "结束：" << summary.period_end << "\n";
    markdown << "来源日记：" << join_zh(summary.source_diary_ids) << "\n\n";
    markdown << summary.summary << "\n";
    write_text(root / "summaries" / (summary.id + ".md"), markdown.str());

    std::ostringstream index;
    index << read_text(root / "memory_index.md");
    index << "- [" << summary.id << "](summaries/" << summary.id
          << ".md) " << summary.period_start << " - " << summary.period_end
          << "\n";
    write_text(root / "memory_index.md", index.str());
}

void AgentStore::upsert_relation(RelationEntry relation) {
    auto record = get_agent(relation.agent_id);
    auto target = get_agent(relation.target_id);
    if (!record.has_value() || !target.has_value()) {
        throw std::runtime_error("relation references unknown agent");
    }
    if (record->world_id != target->world_id) {
        throw std::runtime_error("relation references agents in different worlds");
    }
    relation.intimacy = clamp_intimacy(relation.intimacy);
    if (relation.updated_at.empty()) {
        relation.updated_at = now_iso_utc();
    }

    PgConn conn(*pool_);
    conn.execute(
        "INSERT INTO agent_relations(agent_id, target_id, relation_type, "
        "description, intimacy, key_events, updated_at) "
        "VALUES($1, $2, $3, $4, $5, $6, $7) "
        "ON CONFLICT(agent_id, target_id) DO UPDATE SET "
        "relation_type = EXCLUDED.relation_type, "
        "description = EXCLUDED.description, "
        "intimacy = EXCLUDED.intimacy, "
        "key_events = EXCLUDED.key_events, "
        "updated_at = EXCLUDED.updated_at",
        {relation.agent_id, relation.target_id, relation.relation_type,
         relation.description, std::to_string(relation.intimacy),
         nlohmann::json(relation.key_events).dump(), relation.updated_at});

    std::ostringstream markdown;
    markdown << "# 人际关系\n";
    for (const auto& item : relations_for(relation.agent_id)) {
        markdown << "\n## " << item.target_id << "\n";
        markdown << "关系类型：" << item.relation_type << "\n";
        markdown << "亲密度：" << item.intimacy << "\n";
        markdown << "描述：" << item.description << "\n";
        markdown << "关键事件：" << join_zh(item.key_events) << "\n";
        markdown << "更新时间：" << item.updated_at << "\n";
    }
    write_text(agent_path(relation.agent_id) / "relations.md", markdown.str());
}

std::vector<RelationEntry>
AgentStore::relations_for(const std::string& agent_id) const {
    PgConn conn(*pool_);
    auto res = conn.query(
        "SELECT agent_id, target_id, relation_type, description, intimacy, "
        "key_events, updated_at "
        "FROM agent_relations WHERE agent_id = $1 ORDER BY target_id ASC",
        {agent_id});

    std::vector<RelationEntry> relations;
    for (int i = 0; i < res.ntuples(); i++) {
        relations.push_back(RelationEntry{
            .agent_id = res.get(i, 0),
            .target_id = res.get(i, 1),
            .relation_type = res.get(i, 2),
            .description = res.get(i, 3),
            .updated_at = res.get(i, 6),
            .intimacy = std::stoi(res.get(i, 4)),
            .key_events = nlohmann::json::parse(res.get(i, 5))
                              .get<std::vector<std::string>>(),
        });
    }
    return relations;
}

GroupProfile AgentStore::load_group(const std::string& group_agent_id) const {
    auto record = get_agent(group_agent_id);
    if (!record.has_value() || record->kind != AgentKind::Group) {
        throw std::runtime_error("agent is not a group: " + group_agent_id);
    }
    const auto profile =
        nlohmann::json::parse(read_text(agent_path(group_agent_id) /
                                        "group_profile.json"));
    return GroupProfile{
        .agent_id = profile.at("agent_id").get<std::string>(),
        .culture_card_markdown =
            profile.at("culture_card_markdown").get<std::string>(),
        .member_agent_ids =
            profile.at("member_agent_ids").get<std::vector<std::string>>(),
        .shared_memory_ids =
            profile.at("shared_memory_ids").get<std::vector<std::string>>(),
    };
}

bool AgentStore::can_speak_directly(const std::string& agent_id) const {
    PgConn conn(*pool_);
    auto res = conn.query(
        "SELECT can_speak_directly FROM agent_metadata WHERE agent_id = $1",
        {agent_id});
    if (res.ntuples() > 0) {
        return res.get(0, 0) != "0";
    }
    throw std::runtime_error("missing agent metadata: " + agent_id);
}

std::vector<std::string>
AgentStore::shared_memory_refs_for(const std::string& agent_id) const {
    const auto refs_path = agent_path(agent_id) / "group_memory_refs.json";
    if (!std::filesystem::exists(refs_path)) {
        return {};
    }

    std::vector<std::string> refs;
    for (const auto& group_ref : nlohmann::json::parse(read_text(refs_path))) {
        for (const auto& shared_id : group_ref.at("shared_memory_ids")) {
            refs.push_back(shared_id.get<std::string>());
        }
    }
    return refs;
}

std::vector<DiaryEntry> AgentStore::search_diary(const std::string& agent_id,
                                                  const std::string& keyword,
                                                  int max_results) const {
    PgConn conn(*pool_);
    std::vector<DiaryEntry> results;

    // Try hybrid search (FTS + vector weighting via stored function)
    try {
        auto res = conn.query(
            "SELECT id, agent_id, scene_id, world_time, content, created_at "
            "FROM hybrid_search_diary($1, $2, $3)",
            {agent_id, keyword, std::to_string(std::clamp(max_results, 0, 1000))});

        for (int i = 0; i < res.ntuples(); i++) {
            results.push_back(DiaryEntry{
                .id = res.get(i, 0),
                .agent_id = res.get(i, 1),
                .scene_id = res.get(i, 2),
                .world_time = res.get(i, 3),
                .content = res.get(i, 4),
                .created_at = res.get(i, 5),
            });
        }
    } catch (...) {}

    if (!results.empty()) return results;

    // Fallback to LIKE
    auto res = conn.query(
        "SELECT id, agent_id, scene_id, world_time, content, created_at "
        "FROM agent_diaries "
        "WHERE agent_id = $1 AND content LIKE $2 "
        "ORDER BY created_at DESC, id DESC LIMIT $3",
        {agent_id, "%" + keyword + "%",
         std::to_string(std::clamp(max_results, 0, 1000))});

    for (int i = 0; i < res.ntuples(); i++) {
        results.push_back(DiaryEntry{
            .id = res.get(i, 0),
            .agent_id = res.get(i, 1),
            .scene_id = res.get(i, 2),
            .world_time = res.get(i, 3),
            .content = res.get(i, 4),
            .created_at = res.get(i, 5),
        });
    }
    return results;
}

std::optional<DiaryEntry>
AgentStore::get_diary(const std::string& diary_id) const {
    PgConn conn(*pool_);
    auto res = conn.query(
        "SELECT id, agent_id, scene_id, world_time, content, created_at "
        "FROM agent_diaries WHERE id = $1",
        {diary_id});
    if (res.ntuples() == 0) return std::nullopt;
    return DiaryEntry{
        .id = res.get(0, 0),
        .agent_id = res.get(0, 1),
        .scene_id = res.get(0, 2),
        .world_time = res.get(0, 3),
        .content = res.get(0, 4),
        .created_at = res.get(0, 5),
    };
}

std::vector<AgentRecord>
AgentStore::search_agents_by_traits(const std::string& world_id,
                                     const std::vector<std::string>& traits,
                                     const std::string& identity,
                                     int max_results) const {
    PgConn conn(*pool_);

    std::vector<std::string> params;
    std::ostringstream sql;
    sql << "SELECT a.id, a.world_id, a.name, a.display_name, a.kind, "
           "a.created_at, a.updated_at "
           "FROM agents a "
           "JOIN character_cards c ON c.agent_id = a.id "
           "WHERE a.world_id = $1 AND a.kind = 'individual'";
    int param_idx = 2;
    params.push_back(world_id);

    if (!identity.empty()) {
        sql << " AND c.identity LIKE $" << param_idx++;
        params.push_back("%" + identity + "%");
    }

    if (!traits.empty()) {
        for (const auto& trait : traits) {
            sql << " AND ("
                << "  $" << param_idx << " = ANY(c.core_traits)"
                << "  OR c.background LIKE $" << param_idx + 1
                << "  OR c.knowledge_scope LIKE $" << param_idx + 2
                << "  OR c.appearance LIKE $" << param_idx + 3
                << ")";
            params.push_back(trait);
            params.push_back("%" + trait + "%");
            params.push_back("%" + trait + "%");
            params.push_back("%" + trait + "%");
            param_idx += 4;
        }
    }

    sql << " ORDER BY a.created_at ASC LIMIT $" << param_idx;
    params.push_back(std::to_string(std::clamp(max_results, 0, 100)));

    auto res = conn.query(sql.str(), params);

    std::vector<AgentRecord> results;
    for (int i = 0; i < res.ntuples(); i++) {
        results.push_back(read_agent_record_pg(res, i));
    }
    return results;
}

void AgentStore::update_agent_prompt(const std::string& agent_id,
                                      std::string prompt) {
    PgConn conn(*pool_);
    conn.execute(
        "INSERT INTO agent_prompts (agent_id, prompt, updated_at) "
        "VALUES ($1, $2, $3) "
        "ON CONFLICT (agent_id) DO UPDATE SET prompt = $2, updated_at = $3",
        {agent_id, prompt, now_iso_utc()});
}

std::string AgentStore::load_agent_prompt(const std::string& agent_id) const {
    PgConn conn(*pool_);
    auto result = conn.query(
        "SELECT prompt FROM agent_prompts WHERE agent_id = $1",
        {agent_id});
    if (result.ntuples() == 0) {
        return "";
    }
    return result.get(0, 0);
}

} // namespace merak::worldbuilding
