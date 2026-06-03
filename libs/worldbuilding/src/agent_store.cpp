#include <merak/worldbuilding/agent_store.hpp>

#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/sqlite_helpers.hpp>

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

AgentRecord read_agent_record(Statement& statement) {
    return AgentRecord{
        .id = column_text(statement, 0),
        .world_id = column_text(statement, 1),
        .name = column_text(statement, 2),
        .display_name = column_text(statement, 3),
        .created_at = column_text(statement, 5),
        .updated_at = column_text(statement, 6),
        .kind = agent_kind_from_string(column_text(statement, 4)),
    };
}

void execute_bound(Statement& statement) {
    if (statement.step()) {
        throw std::runtime_error("unexpected sqlite row");
    }
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

nlohmann::json read_json_or_array(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return nlohmann::json::array();
    }
    return nlohmann::json::parse(read_text(path));
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

std::vector<std::string> split_zh_list(const std::string& value) {
    std::vector<std::string> result;
    std::string current;
    for (std::size_t i = 0; i < value.size();) {
        if (value.compare(i, std::string("、").size(), "、") == 0) {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
            i += std::string("、").size();
        } else {
            current.push_back(value[i]);
            ++i;
        }
    }
    if (!current.empty()) {
        result.push_back(current);
    }
    return result;
}

std::string label_value(const std::string& markdown,
                        const std::string& label) {
    const auto prefix = label + "：";
    std::istringstream lines(markdown);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.starts_with(prefix)) {
            return line.substr(prefix.size());
        }
    }
    return {};
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
    return output.str();
}

CharacterCard parse_character_card_markdown(const std::string& markdown) {
    CharacterCard card;
    card.agent_id = label_value(markdown, "Agent ID");
    card.updated_at = label_value(markdown, "更新时间");
    card.name = label_value(markdown, "姓名");
    card.gender = label_value(markdown, "性别");
    card.race = label_value(markdown, "种族");
    card.identity = label_value(markdown, "身份");
    card.core_traits = split_zh_list(label_value(markdown, "核心性格特质"));
    card.emotional_tendency = label_value(markdown, "情绪倾向");
    card.speaking_style = label_value(markdown, "说话风格");
    card.taboo_topics = split_zh_list(label_value(markdown, "禁忌话题"));
    card.core_desire = label_value(markdown, "核心欲望");
    card.deep_fear = label_value(markdown, "深层恐惧");
    card.daily_goal = label_value(markdown, "日常目标");
    card.background = label_value(markdown, "背景故事");
    card.knowledge_scope = label_value(markdown, "知识范围");
    card.appearance = label_value(markdown, "外貌与习惯");

    const auto version = label_value(markdown, "版本");
    card.version = version.empty() ? 1 : std::stoi(version);
    const auto age = label_value(markdown, "年龄");
    card.age = age.empty() ? 0 : std::stoi(age);
    const auto relations = label_value(markdown, "人际关系");
    card.relations =
        relations.empty() ? nlohmann::json::object()
                          : nlohmann::json::parse(relations);
    return card;
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

} // namespace

AgentStore::AgentStore(WorldStore& worlds, std::filesystem::path data_root)
    : worlds_(worlds), data_root_(std::move(data_root)) {
    initialize();
}

void AgentStore::initialize() {
    worlds_.initialize();
    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS agent_diaries(
            id TEXT PRIMARY KEY,
            agent_id TEXT NOT NULL,
            scene_id TEXT NOT NULL,
            world_time TEXT NOT NULL,
            content TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY(agent_id) REFERENCES agents(id) ON DELETE CASCADE
        )
    )sql");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS memory_summaries(
            id TEXT PRIMARY KEY,
            agent_id TEXT NOT NULL,
            period_start TEXT NOT NULL,
            period_end TEXT NOT NULL,
            summary TEXT NOT NULL,
            source_diary_ids TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY(agent_id) REFERENCES agents(id) ON DELETE CASCADE
        )
    )sql");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS agent_relations(
            agent_id TEXT NOT NULL,
            target_id TEXT NOT NULL,
            relation_type TEXT NOT NULL,
            description TEXT NOT NULL,
            intimacy INTEGER NOT NULL,
            key_events TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            PRIMARY KEY(agent_id, target_id),
            FOREIGN KEY(agent_id) REFERENCES agents(id) ON DELETE CASCADE,
            FOREIGN KEY(target_id) REFERENCES agents(id) ON DELETE CASCADE
        )
    )sql");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS agent_metadata(
            agent_id TEXT PRIMARY KEY,
            can_speak_directly INTEGER NOT NULL,
            FOREIGN KEY(agent_id) REFERENCES agents(id) ON DELETE CASCADE
        )
    )sql");
}

std::filesystem::path AgentStore::database_path() const {
    return data_root_ / "worlds.sqlite3";
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

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    Statement insert(db, R"sql(
        INSERT INTO agents(id, world_id, name, display_name, kind,
                           created_at, updated_at)
        VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)
    )sql");
    bind_text(insert, 1, record.id);
    bind_text(insert, 2, record.world_id);
    bind_text(insert, 3, record.name);
    bind_text(insert, 4, record.display_name);
    bind_text(insert, 5, to_string(record.kind));
    bind_text(insert, 6, record.created_at);
    bind_text(insert, 7, record.updated_at);
    execute_bound(insert);

    Statement insert_metadata(db, R"sql(
        INSERT INTO agent_metadata(agent_id, can_speak_directly)
        VALUES(?1, ?2)
    )sql");
    bind_text(insert_metadata, 1, record.id);
    bind_int(insert_metadata, 2, kind == AgentKind::Group ? 0 : 1);
    execute_bound(insert_metadata);
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
    return record;
}

AgentRecord AgentStore::create_group(
    const std::string& world_id, std::string name,
    std::string culture_card_markdown,
    std::vector<std::string> member_agent_ids) {
    auto record = insert_agent(world_id, std::move(name), AgentKind::Group);
    const auto root = worlds_.world_path(world_id) / "agents" / record.id;
    std::filesystem::create_directories(root);

    const auto shared_memory_id = "shared_memory:" + record.id;
    const auto shared_memory_ids = std::vector<std::string>{shared_memory_id};
    const auto profile = nlohmann::json{
        {"agent_id", record.id},
        {"culture_card_markdown", culture_card_markdown},
        {"member_agent_ids", member_agent_ids},
        {"shared_memory_ids", shared_memory_ids},
        {"can_speak_directly", false},
    };

    write_text(root / "group_profile.json", profile.dump(2));
    write_text(root / "culture_card.md", culture_card_markdown);
    write_text(root / "members.json", nlohmann::json(member_agent_ids).dump(2));
    write_text(root / "shared_memory_refs.json",
               nlohmann::json(shared_memory_ids).dump(2));

    for (const auto& member_agent_id : member_agent_ids) {
        const auto member_path = agent_path(member_agent_id);
        const auto refs_path = member_path / "group_memory_refs.json";
        auto refs = read_json_or_array(refs_path);
        refs.push_back({
            {"group_agent_id", record.id},
            {"shared_memory_ids", shared_memory_ids},
        });
        write_text(refs_path, refs.dump(2));

        std::ostringstream index;
        index << read_text(member_path / "memory_index.md");
        index << "- 群体共享记忆：" << record.id << " -> "
              << join_zh(shared_memory_ids) << "\n";
        write_text(member_path / "memory_index.md", index.str());
    }
    return record;
}

std::optional<AgentRecord>
AgentStore::get_agent(const std::string& agent_id) const {
    SqliteDb db(database_path().string());
    Statement query(db, R"sql(
        SELECT id, world_id, name, display_name, kind, created_at, updated_at
        FROM agents
        WHERE id = ?1
    )sql");
    bind_text(query, 1, agent_id);
    if (!query.step()) {
        return std::nullopt;
    }
    return read_agent_record(query);
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

    SqliteDb db(database_path().string());
    Statement update(db, R"sql(
        UPDATE agents SET name = ?1, display_name = ?2, updated_at = ?3
        WHERE id = ?4
    )sql");
    bind_text(update, 1, next_card.name);
    bind_text(update, 2, next_card.name);
    bind_text(update, 3, next_card.updated_at);
    bind_text(update, 4, agent_id);
    execute_bound(update);
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

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    Statement insert(db, R"sql(
        INSERT INTO agent_diaries(id, agent_id, scene_id, world_time, content,
                                  created_at)
        VALUES(?1, ?2, ?3, ?4, ?5, ?6)
    )sql");
    bind_text(insert, 1, entry.id);
    bind_text(insert, 2, entry.agent_id);
    bind_text(insert, 3, entry.scene_id);
    bind_text(insert, 4, entry.world_time);
    bind_text(insert, 5, entry.content);
    bind_text(insert, 6, entry.created_at);
    execute_bound(insert);

    const auto root = agent_path(entry.agent_id);
    std::ostringstream diary;
    diary << "# 场景日记：" << entry.scene_id << "\n\n";
    diary << "ID：" << entry.id << "\n";
    diary << "世界时间：" << entry.world_time << "\n";
    diary << "创建时间：" << entry.created_at << "\n\n";
    diary << entry.content << "\n";
    write_text(root / "diary" / (entry.scene_id + ".md"), diary.str());

    std::ostringstream index;
    index << read_text(root / "memory_index.md");
    index << "- [" << entry.scene_id << "](diary/" << entry.scene_id
          << ".md) " << entry.world_time << " " << entry.id << "\n";
    write_text(root / "memory_index.md", index.str());
}

std::vector<DiaryEntry>
AgentStore::recent_diary(const std::string& agent_id, int max_entries) const {
    SqliteDb db(database_path().string());
    Statement query(db, R"sql(
        SELECT id, agent_id, scene_id, world_time, content, created_at
        FROM agent_diaries
        WHERE agent_id = ?1
        ORDER BY created_at DESC, id DESC
        LIMIT ?2
    )sql");
    bind_text(query, 1, agent_id);
    bind_int(query, 2, std::max(0, max_entries));

    std::vector<DiaryEntry> entries;
    while (query.step()) {
        entries.push_back(DiaryEntry{
            .id = column_text(query, 0),
            .agent_id = column_text(query, 1),
            .scene_id = column_text(query, 2),
            .world_time = column_text(query, 3),
            .content = column_text(query, 4),
            .created_at = column_text(query, 5),
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

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    Statement insert(db, R"sql(
        INSERT INTO memory_summaries(id, agent_id, period_start, period_end,
                                     summary, source_diary_ids, created_at)
        VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)
    )sql");
    bind_text(insert, 1, summary.id);
    bind_text(insert, 2, summary.agent_id);
    bind_text(insert, 3, summary.period_start);
    bind_text(insert, 4, summary.period_end);
    bind_text(insert, 5, summary.summary);
    bind_text(insert, 6, nlohmann::json(summary.source_diary_ids).dump());
    bind_text(insert, 7, summary.created_at);
    execute_bound(insert);

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
    relation.intimacy = clamp_intimacy(relation.intimacy);
    if (relation.updated_at.empty()) {
        relation.updated_at = now_iso_utc();
    }

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    Statement upsert(db, R"sql(
        INSERT INTO agent_relations(agent_id, target_id, relation_type,
                                    description, intimacy, key_events,
                                    updated_at)
        VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)
        ON CONFLICT(agent_id, target_id) DO UPDATE SET
            relation_type = excluded.relation_type,
            description = excluded.description,
            intimacy = excluded.intimacy,
            key_events = excluded.key_events,
            updated_at = excluded.updated_at
    )sql");
    bind_text(upsert, 1, relation.agent_id);
    bind_text(upsert, 2, relation.target_id);
    bind_text(upsert, 3, relation.relation_type);
    bind_text(upsert, 4, relation.description);
    bind_int(upsert, 5, relation.intimacy);
    bind_text(upsert, 6, nlohmann::json(relation.key_events).dump());
    bind_text(upsert, 7, relation.updated_at);
    execute_bound(upsert);

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
    SqliteDb db(database_path().string());
    Statement query(db, R"sql(
        SELECT agent_id, target_id, relation_type, description, intimacy,
               key_events, updated_at
        FROM agent_relations
        WHERE agent_id = ?1
        ORDER BY target_id ASC
    )sql");
    bind_text(query, 1, agent_id);

    std::vector<RelationEntry> relations;
    while (query.step()) {
        relations.push_back(RelationEntry{
            .agent_id = column_text(query, 0),
            .target_id = column_text(query, 1),
            .relation_type = column_text(query, 2),
            .description = column_text(query, 3),
            .updated_at = column_text(query, 6),
            .intimacy = column_int(query, 4),
            .key_events =
                nlohmann::json::parse(column_text(query, 5))
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
    SqliteDb db(database_path().string());
    Statement query(db, R"sql(
        SELECT can_speak_directly
        FROM agent_metadata
        WHERE agent_id = ?1
    )sql");
    bind_text(query, 1, agent_id);
    if (query.step()) {
        return column_int(query, 0) != 0;
    }

    const auto record = get_agent(agent_id);
    if (!record.has_value()) {
        throw std::runtime_error("unknown agent: " + agent_id);
    }
    return record->kind != AgentKind::Group;
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

} // namespace merak::worldbuilding
