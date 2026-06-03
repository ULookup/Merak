#include <merak/worldbuilding/foreshadowing_store.hpp>

#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/sqlite_helpers.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace merak::worldbuilding {
namespace {

void execute_bound(Statement& statement) {
    if (statement.step()) {
        throw std::runtime_error("unexpected sqlite row");
    }
}

void rollback_no_throw(SqliteDb& db) noexcept {
    try {
        db.exec("ROLLBACK");
    } catch (...) {
    }
}

void write_json(const std::filesystem::path& path, const nlohmann::json& json) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to write " + path.string());
    }
    output << json.dump(2);
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to read " + path.string());
    }
    return nlohmann::json::parse(input);
}

ForeshadowStatus foreshadow_status_from_string(const std::string& value) {
    if (value == "open") return ForeshadowStatus::Open;
    if (value == "paid") return ForeshadowStatus::Paid;
    if (value == "abandoned") return ForeshadowStatus::Abandoned;
    throw std::runtime_error("unknown foreshadow status: " + value);
}

ForeshadowHintLevel hint_level_from_string(const std::string& value) {
    if (value == "subtle") return ForeshadowHintLevel::Subtle;
    if (value == "visible") return ForeshadowHintLevel::Visible;
    if (value == "obvious") return ForeshadowHintLevel::Obvious;
    throw std::runtime_error("unknown hint level: " + value);
}

ForeshadowCreatedBy created_by_from_string(const std::string& value) {
    if (value == "author") return ForeshadowCreatedBy::Author;
    if (value == "god_agent_detected") return ForeshadowCreatedBy::GodAgentDetected;
    throw std::runtime_error("unknown created_by: " + value);
}

nlohmann::json foreshadowing_json(const Foreshadowing& item) {
    nlohmann::json json{
        {"id", item.id},
        {"content", item.content},
        {"pay_off_idea", item.pay_off_idea},
        {"status", to_string(item.status)},
        {"hint_level", to_string(item.hint_level)},
        {"tags", item.tags},
        {"related_foreshadowing_ids", item.related_foreshadowing_ids},
        {"related_secret_ids", item.related_secret_ids},
        {"created_by", to_string(item.created_by)},
    };
    json["planted_at"] = item.planted_at.has_value() ? nlohmann::json(*item.planted_at) : nlohmann::json(nullptr);
    json["paid_at"] = item.paid_at.has_value() ? nlohmann::json(*item.paid_at) : nlohmann::json(nullptr);
    return json;
}

Foreshadowing foreshadowing_from_json(const nlohmann::json& json) {
    Foreshadowing item;
    item.id = json.at("id").get<std::string>();
    item.content = json.at("content").get<std::string>();
    item.pay_off_idea = json.at("pay_off_idea").get<std::string>();
    item.status = foreshadow_status_from_string(json.at("status").get<std::string>());
    item.hint_level = hint_level_from_string(json.at("hint_level").get<std::string>());
    item.tags = json.at("tags").get<std::vector<std::string>>();
    item.related_foreshadowing_ids = json.at("related_foreshadowing_ids").get<std::vector<std::string>>();
    item.related_secret_ids = json.at("related_secret_ids").get<std::vector<std::string>>();
    item.created_by = created_by_from_string(json.at("created_by").get<std::string>());
    if (!json.at("planted_at").is_null()) {
        item.planted_at = json.at("planted_at").get<std::string>();
    }
    if (!json.at("paid_at").is_null()) {
        item.paid_at = json.at("paid_at").get<std::string>();
    }
    return item;
}

void ensure_world_exists(WorldStore& worlds, const std::string& world_id) {
    if (!worlds.get_world(world_id).has_value()) {
        throw std::runtime_error("unknown world: " + world_id);
    }
}

} // namespace

ForeshadowingStore::ForeshadowingStore(WorldStore& worlds,
                                       NarrativeStore& narrative,
                                       std::filesystem::path data_root)
    : worlds_(worlds), narrative_(narrative), data_root_(std::move(data_root)) {
    initialize();
}

void ForeshadowingStore::initialize() {
    worlds_.initialize();
    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS foreshadowings(
            id TEXT PRIMARY KEY,
            world_id TEXT NOT NULL,
            status TEXT NOT NULL,
            hint_level TEXT NOT NULL,
            tags TEXT NOT NULL,
            created_by TEXT NOT NULL,
            FOREIGN KEY(world_id) REFERENCES worlds(id) ON DELETE CASCADE
        )
    )sql");
    db.exec(R"sql(
        CREATE INDEX IF NOT EXISTS foreshadowings_by_status
        ON foreshadowings(world_id, status, id)
    )sql");
    db.exec(R"sql(
        CREATE INDEX IF NOT EXISTS foreshadowings_by_tag
        ON foreshadowings(world_id, id)
    )sql");
}

std::filesystem::path ForeshadowingStore::database_path() const {
    return data_root_ / "worlds.sqlite3";
}

Foreshadowing ForeshadowingStore::plant(const std::string& world_id,
                                        Foreshadowing item) {
    ensure_world_exists(worlds_, world_id);
    if (item.id.empty()) {
        item.id = make_id("foreshadow");
    }
    item.status = ForeshadowStatus::Open;

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    db.exec("BEGIN");
    try {
        Statement insert(db, R"sql(
            INSERT INTO foreshadowings(id, world_id, status, hint_level, tags, created_by)
            VALUES(?1, ?2, ?3, ?4, ?5, ?6)
        )sql");
        bind_text(insert, 1, item.id);
        bind_text(insert, 2, world_id);
        bind_text(insert, 3, to_string(item.status));
        bind_text(insert, 4, to_string(item.hint_level));
        bind_text(insert, 5, nlohmann::json(item.tags).dump());
        bind_text(insert, 6, to_string(item.created_by));
        execute_bound(insert);

        write_json(worlds_.world_path(world_id) / "foreshadows" / (item.id + ".json"),
                   foreshadowing_json(item));
        db.exec("COMMIT");
    } catch (...) {
        rollback_no_throw(db);
        throw;
    }
    return item;
}

Foreshadowing ForeshadowingStore::pay(const std::string& world_id,
                                       const std::string& id,
                                       const std::string& scene_id) {
    ensure_world_exists(worlds_, world_id);
    const auto path = worlds_.world_path(world_id) / "foreshadows" / (id + ".json");
    auto json = read_json(path);

    auto item = foreshadowing_from_json(json);
    item.status = ForeshadowStatus::Paid;
    item.paid_at = scene_id;

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    db.exec("BEGIN");
    try {
        // Persist the payoff JSON
        write_json(path, foreshadowing_json(item));

        // Update SQLite status within the transaction
        Statement update(db, R"sql(
            UPDATE foreshadowings SET status = ?1 WHERE world_id = ?2 AND id = ?3
        )sql");
        bind_text(update, 1, to_string(item.status));
        bind_text(update, 2, world_id);
        bind_text(update, 3, id);
        execute_bound(update);

        // Update chapter linkage: move from planted to paid
        Statement query_chapters(db, R"sql(
            SELECT id FROM chapters WHERE world_id = ?1
        )sql");
        bind_text(query_chapters, 1, world_id);
        while (query_chapters.step()) {
            const auto chapter_id = column_text(query_chapters, 0);
            const auto chapter_path = worlds_.world_path(world_id) / "chapters" / (chapter_id + ".json");
            if (!std::filesystem::exists(chapter_path)) continue;
            auto chapter_json = read_json(chapter_path);

            auto planted = chapter_json.at("foreshadowing_planted").get<std::vector<std::string>>();
            auto it = std::find(planted.begin(), planted.end(), id);
            if (it != planted.end()) {
                planted.erase(it);
                chapter_json["foreshadowing_planted"] = planted;

                auto paid = chapter_json.at("foreshadowing_paid").get<std::vector<std::string>>();
                paid.push_back(id);
                chapter_json["foreshadowing_paid"] = paid;

                write_json(chapter_path, chapter_json);
            }
        }
        db.exec("COMMIT");
    } catch (...) {
        rollback_no_throw(db);
        // Roll back the JSON file to its original state
        write_json(path, json);
        throw;
    }
    return item;
}

Foreshadowing ForeshadowingStore::abandon(const std::string& world_id,
                                           const std::string& id) {
    ensure_world_exists(worlds_, world_id);
    const auto path = worlds_.world_path(world_id) / "foreshadows" / (id + ".json");
    auto json = read_json(path);

    auto item = foreshadowing_from_json(json);
    item.status = ForeshadowStatus::Abandoned;

    write_json(path, foreshadowing_json(item));

    SqliteDb db(database_path().string());
    Statement update(db, R"sql(
        UPDATE foreshadowings SET status = ?1 WHERE world_id = ?2 AND id = ?3
    )sql");
    bind_text(update, 1, to_string(item.status));
    bind_text(update, 2, world_id);
    bind_text(update, 3, id);
    execute_bound(update);

    return item;
}

std::vector<Foreshadowing>
ForeshadowingStore::list(const std::string& world_id,
                          std::optional<ForeshadowStatus> status) const {
    ensure_world_exists(worlds_, world_id);
    std::vector<Foreshadowing> result;
    const auto base = worlds_.world_path(world_id) / "foreshadows";

    if (!std::filesystem::exists(base)) {
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        auto item = foreshadowing_from_json(read_json(entry.path()));
        if (!status.has_value() || item.status == *status) {
            result.push_back(std::move(item));
        }
    }

    return result;
}

std::vector<Foreshadowing>
ForeshadowingStore::relevant_for_scene(const std::string& world_id,
                                        const Scene& scene) const {
    ensure_world_exists(worlds_, world_id);
    std::vector<Foreshadowing> result;
    const auto base = worlds_.world_path(world_id) / "foreshadows";

    if (!std::filesystem::exists(base)) {
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        auto item = foreshadowing_from_json(read_json(entry.path()));
        if (item.status != ForeshadowStatus::Open) continue;

        // Match by participant IDs
        for (const auto& pid : scene.participant_ids) {
            if (std::find(item.tags.begin(), item.tags.end(), pid) != item.tags.end()) {
                result.push_back(std::move(item));
                goto next;
            }
        }
        // Match by location tag
        if (scene.location_id.has_value()) {
            if (std::find(item.tags.begin(), item.tags.end(), *scene.location_id) != item.tags.end()) {
                result.push_back(std::move(item));
            }
        }
        next:;
    }

    return result;
}

ForeshadowStats ForeshadowingStore::stats(const std::string& world_id) const {
    ensure_world_exists(worlds_, world_id);
    ForeshadowStats result;
    const auto base = worlds_.world_path(world_id) / "foreshadows";

    if (!std::filesystem::exists(base)) {
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        auto item = foreshadowing_from_json(read_json(entry.path()));
        switch (item.status) {
        case ForeshadowStatus::Open: ++result.open; break;
        case ForeshadowStatus::Paid: ++result.paid; break;
        case ForeshadowStatus::Abandoned: ++result.abandoned; break;
        }
    }

    return result;
}

ForeshadowStats
ForeshadowingStore::chapter_summary(const std::string& world_id,
                                     const std::string& chapter_id) const {
    ensure_world_exists(worlds_, world_id);
    const auto chapter_path = worlds_.world_path(world_id) / "chapters" / (chapter_id + ".json");
    if (!std::filesystem::exists(chapter_path)) {
        throw std::runtime_error("unknown chapter: " + chapter_id);
    }

    const auto chapter_json = read_json(chapter_path);
    const auto planted = chapter_json.at("foreshadowing_planted").get<std::vector<std::string>>();
    const auto paid = chapter_json.at("foreshadowing_paid").get<std::vector<std::string>>();

    ForeshadowStats result;
    const auto base = worlds_.world_path(world_id) / "foreshadows";

    for (const auto& id : planted) {
        const auto path = base / (id + ".json");
        if (!std::filesystem::exists(path)) continue;
        auto item = foreshadowing_from_json(read_json(path));
        if (item.status == ForeshadowStatus::Open) ++result.open;
        else if (item.status == ForeshadowStatus::Paid) ++result.paid;
        else if (item.status == ForeshadowStatus::Abandoned) ++result.abandoned;
    }
    for (const auto& id : paid) {
        const auto path = base / (id + ".json");
        if (!std::filesystem::exists(path)) continue;
        auto item = foreshadowing_from_json(read_json(path));
        if (item.status == ForeshadowStatus::Paid) ++result.paid;
    }

    return result;
}

std::vector<Foreshadowing>
ForeshadowingStore::final_act_reminders(const std::string& world_id) const {
    ensure_world_exists(worlds_, world_id);
    std::vector<Foreshadowing> result;

    const auto structure_path = worlds_.world_path(world_id) / "story_structure.json";
    if (!std::filesystem::exists(structure_path)) {
        return result;
    }

    const auto structure = read_json(structure_path);
    const auto stages = structure.at("stages").get<std::vector<std::string>>();
    if (stages.empty()) return result;

    const auto& current_stage = stages.back();

    // Only return reminders when in the final stage
    if (current_stage != "解决" && current_stage != "归返") {
        return result;
    }

    const auto base = worlds_.world_path(world_id) / "foreshadows";
    if (!std::filesystem::exists(base)) return result;

    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        auto item = foreshadowing_from_json(read_json(entry.path()));
        if (item.status == ForeshadowStatus::Open) {
            result.push_back(std::move(item));
        }
    }

    return result;
}

} // namespace merak::worldbuilding
