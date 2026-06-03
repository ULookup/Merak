#include <merak/worldbuilding/narrative_store.hpp>

#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/sqlite_helpers.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

ChapterStatus chapter_status_from_string(const std::string& value) {
    if (value == "outline") {
        return ChapterStatus::Outline;
    }
    if (value == "drafting") {
        return ChapterStatus::Drafting;
    }
    if (value == "completed") {
        return ChapterStatus::Completed;
    }
    if (value == "revised") {
        return ChapterStatus::Revised;
    }
    throw std::runtime_error("unknown chapter status: " + value);
}

SceneStatus scene_status_from_string(const std::string& value) {
    if (value == "draft") {
        return SceneStatus::Draft;
    }
    if (value == "writing") {
        return SceneStatus::Writing;
    }
    if (value == "completed") {
        return SceneStatus::Completed;
    }
    throw std::runtime_error("unknown scene status: " + value);
}

nlohmann::json story_structure_json(const StoryStructure& structure) {
    return nlohmann::json{{"template_type", to_string(structure.template_type)},
                          {"name", structure.name},
                          {"stages", structure.stages}};
}

nlohmann::json arc_json(const Arc& arc) {
    nlohmann::json json{{"id", arc.id},
                        {"title", arc.title},
                        {"purpose", arc.purpose},
                        {"status", arc.status},
                        {"chapter_numbers", arc.chapter_numbers}};
    json["climax_scene_id"] =
        arc.climax_scene_id.has_value() ? nlohmann::json(*arc.climax_scene_id) :
                                          nlohmann::json(nullptr);
    return json;
}

Arc arc_from_json(const nlohmann::json& json) {
    Arc arc;
    arc.id = json.at("id").get<std::string>();
    arc.title = json.at("title").get<std::string>();
    arc.purpose = json.at("purpose").get<std::string>();
    arc.status = json.at("status").get<std::string>();
    arc.chapter_numbers =
        json.at("chapter_numbers").get<std::vector<int>>();
    if (!json.at("climax_scene_id").is_null()) {
        arc.climax_scene_id = json.at("climax_scene_id").get<std::string>();
    }
    return arc;
}

nlohmann::json chapter_json(const Chapter& chapter) {
    nlohmann::json json{{"id", chapter.id},
                        {"title", chapter.title},
                        {"pitch", chapter.pitch},
                        {"notes", chapter.notes},
                        {"number", chapter.number},
                        {"status", to_string(chapter.status)},
                        {"emotional_curve", chapter.emotional_curve},
                        {"scene_ids", chapter.scene_ids},
                        {"foreshadowing_planted",
                         chapter.foreshadowing_planted},
                        {"foreshadowing_paid", chapter.foreshadowing_paid}};
    json["arc_id"] = chapter.arc_id.has_value() ?
                         nlohmann::json(*chapter.arc_id) :
                         nlohmann::json(nullptr);
    return json;
}

Chapter chapter_from_json(const nlohmann::json& json) {
    Chapter chapter;
    chapter.id = json.at("id").get<std::string>();
    chapter.title = json.at("title").get<std::string>();
    chapter.pitch = json.at("pitch").get<std::string>();
    chapter.notes = json.at("notes").get<std::string>();
    chapter.number = json.at("number").get<int>();
    if (!json.at("arc_id").is_null()) {
        chapter.arc_id = json.at("arc_id").get<std::string>();
    }
    chapter.status =
        chapter_status_from_string(json.at("status").get<std::string>());
    chapter.emotional_curve = json.at("emotional_curve");
    chapter.scene_ids = json.at("scene_ids").get<std::vector<std::string>>();
    chapter.foreshadowing_planted =
        json.at("foreshadowing_planted").get<std::vector<std::string>>();
    chapter.foreshadowing_paid =
        json.at("foreshadowing_paid").get<std::vector<std::string>>();
    return chapter;
}

nlohmann::json section_json(const Section& section) {
    return nlohmann::json{{"id", section.id},
                          {"chapter_id", section.chapter_id},
                          {"title", section.title},
                          {"order", section.order},
                          {"scene_ids", section.scene_ids}};
}

Section section_from_json(const nlohmann::json& json) {
    Section section;
    section.id = json.at("id").get<std::string>();
    section.chapter_id = json.at("chapter_id").get<std::string>();
    section.title = json.at("title").get<std::string>();
    section.order = json.at("order").get<int>();
    section.scene_ids = json.at("scene_ids").get<std::vector<std::string>>();
    return section;
}

nlohmann::json scene_json(const Scene& scene, bool is_flashback = false) {
    nlohmann::json json{{"id", scene.id},
                        {"title", scene.title},
                        {"chapter_id", scene.chapter_id},
                        {"world_time", scene.world_time},
                        {"narrative", scene.narrative},
                        {"participant_ids", scene.participant_ids},
                        {"status", to_string(scene.status)},
                        {"is_flashback", is_flashback}};
    json["section_id"] = scene.section_id.has_value() ?
                             nlohmann::json(*scene.section_id) :
                             nlohmann::json(nullptr);
    json["location_id"] = scene.location_id.has_value() ?
                              nlohmann::json(*scene.location_id) :
                              nlohmann::json(nullptr);
    return json;
}

Scene scene_from_json(const nlohmann::json& json) {
    Scene scene;
    scene.id = json.at("id").get<std::string>();
    scene.title = json.at("title").get<std::string>();
    scene.chapter_id = json.at("chapter_id").get<std::string>();
    scene.world_time = json.at("world_time").get<std::string>();
    scene.narrative = json.at("narrative").get<std::string>();
    if (!json.at("section_id").is_null()) {
        scene.section_id = json.at("section_id").get<std::string>();
    }
    if (!json.at("location_id").is_null()) {
        scene.location_id = json.at("location_id").get<std::string>();
    }
    scene.participant_ids =
        json.at("participant_ids").get<std::vector<std::string>>();
    scene.status = scene_status_from_string(json.at("status").get<std::string>());
    return scene;
}

nlohmann::json timeline_event_json(const TimelineEvent& event) {
    return nlohmann::json{{"id", event.id},
                          {"world_time", event.world_time},
                          {"description", event.description},
                          {"recorded_by", event.recorded_by},
                          {"affected_character_ids",
                           event.affected_character_ids},
                          {"related_scene_ids", event.related_scene_ids}};
}

std::string first_line_summary(const std::string& markdown) {
    std::istringstream input(markdown);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            return line;
        }
    }
    return {};
}

bool overlaps(const std::vector<std::string>& left,
              const std::vector<std::string>& right) {
    for (const auto& value : left) {
        if (std::find(right.begin(), right.end(), value) != right.end()) {
            return true;
        }
    }
    return false;
}

void ensure_world_exists(WorldStore& worlds, const std::string& world_id) {
    if (!worlds.get_world(world_id).has_value()) {
        throw std::runtime_error("unknown world: " + world_id);
    }
}

} // namespace

NarrativeStore::NarrativeStore(WorldStore& worlds, std::filesystem::path data_root)
    : worlds_(worlds), data_root_(std::move(data_root)) {
    initialize();
}

void NarrativeStore::initialize() {
    worlds_.initialize();
    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS arcs(
            id TEXT PRIMARY KEY,
            world_id TEXT NOT NULL,
            title TEXT NOT NULL,
            purpose TEXT NOT NULL,
            status TEXT NOT NULL,
            FOREIGN KEY(world_id) REFERENCES worlds(id) ON DELETE CASCADE
        )
    )sql");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS chapters(
            id TEXT PRIMARY KEY,
            world_id TEXT NOT NULL,
            arc_id TEXT,
            number INTEGER NOT NULL,
            title TEXT NOT NULL,
            status TEXT NOT NULL,
            FOREIGN KEY(world_id) REFERENCES worlds(id) ON DELETE CASCADE
        )
    )sql");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS sections(
            id TEXT PRIMARY KEY,
            world_id TEXT NOT NULL,
            chapter_id TEXT NOT NULL,
            display_order INTEGER NOT NULL,
            title TEXT NOT NULL,
            FOREIGN KEY(world_id) REFERENCES worlds(id) ON DELETE CASCADE,
            FOREIGN KEY(chapter_id) REFERENCES chapters(id) ON DELETE CASCADE
        )
    )sql");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS scenes(
            id TEXT PRIMARY KEY,
            world_id TEXT NOT NULL,
            chapter_id TEXT NOT NULL,
            section_id TEXT,
            world_time TEXT NOT NULL,
            title TEXT NOT NULL,
            status TEXT NOT NULL,
            is_flashback INTEGER NOT NULL DEFAULT 0,
            participants TEXT NOT NULL,
            FOREIGN KEY(world_id) REFERENCES worlds(id) ON DELETE CASCADE,
            FOREIGN KEY(chapter_id) REFERENCES chapters(id) ON DELETE CASCADE
        )
    )sql");
    db.exec(R"sql(
        CREATE INDEX IF NOT EXISTS scenes_by_world_time
        ON scenes(world_id, world_time, id)
    )sql");
}

std::filesystem::path NarrativeStore::database_path() const {
    return data_root_ / "worlds.sqlite3";
}

StoryStructure
NarrativeStore::create_story_structure(const std::string& world_id,
                                       NarrativeTemplate type) {
    ensure_world_exists(worlds_, world_id);

    StoryStructure structure;
    structure.template_type = type;
    structure.name = to_string(type);
    switch (type) {
    case NarrativeTemplate::ThreeAct:
        structure.stages = {"建立", "对抗", "解决"};
        break;
    case NarrativeTemplate::FourAct:
        structure.stages = {"建立", "承压", "反转", "解决"};
        break;
    case NarrativeTemplate::HerosJourney:
        structure.stages = {"召唤", "试炼", "归返"};
        break;
    case NarrativeTemplate::Freeform:
        structure.stages = {};
        break;
    }

    write_json(worlds_.world_path(world_id) / "story_structure.json",
               story_structure_json(structure));
    return structure;
}

Arc NarrativeStore::create_arc(const std::string& world_id, Arc arc) {
    ensure_world_exists(worlds_, world_id);
    if (arc.id.empty()) {
        arc.id = make_id("arc");
    }

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    Statement insert(db, R"sql(
        INSERT INTO arcs(id, world_id, title, purpose, status)
        VALUES(?1, ?2, ?3, ?4, ?5)
    )sql");
    bind_text(insert, 1, arc.id);
    bind_text(insert, 2, world_id);
    bind_text(insert, 3, arc.title);
    bind_text(insert, 4, arc.purpose);
    bind_text(insert, 5, arc.status);
    execute_bound(insert);

    write_json(worlds_.world_path(world_id) / "arcs" / (arc.id + ".json"),
               arc_json(arc));
    return arc;
}

Chapter NarrativeStore::create_chapter(const std::string& world_id,
                                       Chapter chapter) {
    ensure_world_exists(worlds_, world_id);
    if (chapter.id.empty()) {
        chapter.id = make_id("chapter");
    }
    if (chapter.arc_id.has_value() &&
        !std::filesystem::exists(worlds_.world_path(world_id) / "arcs" /
                                 (*chapter.arc_id + ".json"))) {
        throw std::runtime_error("unknown arc: " + *chapter.arc_id);
    }

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    db.exec("BEGIN");
    try {
        {
            Statement insert(db, R"sql(
                INSERT INTO chapters(id, world_id, arc_id, number, title, status)
                VALUES(?1, ?2, ?3, ?4, ?5, ?6)
            )sql");
            bind_text(insert, 1, chapter.id);
            bind_text(insert, 2, world_id);
            bind_text(insert, 3, chapter.arc_id.value_or(""));
            bind_int(insert, 4, chapter.number);
            bind_text(insert, 5, chapter.title);
            bind_text(insert, 6, to_string(chapter.status));
            execute_bound(insert);
        }
        if (chapter.arc_id.has_value()) {
            const auto arc_path =
                worlds_.world_path(world_id) / "arcs" / (*chapter.arc_id + ".json");
            auto arc = arc_from_json(read_json(arc_path));
            arc.chapter_numbers.push_back(chapter.number);
            write_json(arc_path, arc_json(arc));
        }
        write_json(worlds_.world_path(world_id) / "chapters" /
                       (chapter.id + ".json"),
                   chapter_json(chapter));
        std::filesystem::create_directories(worlds_.world_path(world_id) /
                                            "chapters" / chapter.id /
                                            "sections");
        db.exec("COMMIT");
    } catch (...) {
        rollback_no_throw(db);
        throw;
    }
    return chapter;
}

Section NarrativeStore::create_section(const std::string& world_id,
                                       Section section) {
    ensure_world_exists(worlds_, world_id);
    if (section.chapter_id.empty()) {
        throw std::runtime_error("section requires chapter");
    }
    const auto chapter_path =
        worlds_.world_path(world_id) / "chapters" / (section.chapter_id + ".json");
    if (!std::filesystem::exists(chapter_path)) {
        throw std::runtime_error("unknown chapter: " + section.chapter_id);
    }
    if (section.id.empty()) {
        section.id = make_id("section");
    }

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    Statement insert(db, R"sql(
        INSERT INTO sections(id, world_id, chapter_id, display_order, title)
        VALUES(?1, ?2, ?3, ?4, ?5)
    )sql");
    bind_text(insert, 1, section.id);
    bind_text(insert, 2, world_id);
    bind_text(insert, 3, section.chapter_id);
    bind_int(insert, 4, section.order);
    bind_text(insert, 5, section.title);
    execute_bound(insert);

    write_json(worlds_.world_path(world_id) / "chapters" / section.chapter_id /
                   "sections" / (section.id + ".json"),
               section_json(section));
    return section;
}

Scene NarrativeStore::create_scene(const std::string& world_id, Scene scene) {
    ensure_world_exists(worlds_, world_id);
    if (scene.chapter_id.empty()) {
        throw std::runtime_error("scene requires chapter");
    }
    if (scene.world_time.empty()) {
        throw std::runtime_error("scene requires world_time");
    }
    if (scene.participant_ids.empty()) {
        throw std::runtime_error("scene requires participants");
    }
    const auto chapter_path =
        worlds_.world_path(world_id) / "chapters" / (scene.chapter_id + ".json");
    if (!std::filesystem::exists(chapter_path)) {
        throw std::runtime_error("unknown chapter: " + scene.chapter_id);
    }
    if (scene.section_id.has_value()) {
        const auto section_path = worlds_.world_path(world_id) / "chapters" /
                                  scene.chapter_id / "sections" /
                                  (*scene.section_id + ".json");
        if (!std::filesystem::exists(section_path)) {
            throw std::runtime_error("unknown section: " + *scene.section_id);
        }
    }
    if (scene.id.empty()) {
        scene.id = make_id("scene");
    }

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    db.exec("BEGIN");
    try {
        {
            Statement insert(db, R"sql(
                INSERT INTO scenes(id, world_id, chapter_id, section_id,
                                   world_time, title, status, is_flashback,
                                   participants)
                VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, 0, ?8)
            )sql");
            bind_text(insert, 1, scene.id);
            bind_text(insert, 2, world_id);
            bind_text(insert, 3, scene.chapter_id);
            bind_text(insert, 4, scene.section_id.value_or(""));
            bind_text(insert, 5, scene.world_time);
            bind_text(insert, 6, scene.title);
            bind_text(insert, 7, to_string(scene.status));
            bind_text(insert, 8, nlohmann::json(scene.participant_ids).dump());
            execute_bound(insert);
        }

        auto chapter = chapter_from_json(read_json(chapter_path));
        chapter.scene_ids.push_back(scene.id);
        write_json(chapter_path, chapter_json(chapter));

        if (scene.section_id.has_value()) {
            const auto section_path = worlds_.world_path(world_id) / "chapters" /
                                      scene.chapter_id / "sections" /
                                      (*scene.section_id + ".json");
            auto section = section_from_json(read_json(section_path));
            section.scene_ids.push_back(scene.id);
            write_json(section_path, section_json(section));
        }

        write_json(worlds_.world_path(world_id) / "scenes" /
                       (scene.id + ".json"),
                   scene_json(scene));
        db.exec("COMMIT");
    } catch (...) {
        rollback_no_throw(db);
        throw;
    }

    return scene;
}

Scene NarrativeStore::update_scene_status(const std::string& world_id,
                                          const std::string& scene_id,
                                          SceneStatus status) {
    ensure_world_exists(worlds_, world_id);
    const auto path =
        worlds_.world_path(world_id) / "scenes" / (scene_id + ".json");
    auto json = read_json(path);
    json["status"] = to_string(status);
    write_json(path, json);

    SqliteDb db(database_path().string());
    Statement update(db, R"sql(
        UPDATE scenes SET status = ?1 WHERE world_id = ?2 AND id = ?3
    )sql");
    bind_text(update, 1, to_string(status));
    bind_text(update, 2, world_id);
    bind_text(update, 3, scene_id);
    execute_bound(update);

    return scene_from_json(json);
}

Scene NarrativeStore::append_scene_text(const std::string& world_id,
                                        const std::string& scene_id,
                                        std::string markdown) {
    ensure_world_exists(worlds_, world_id);
    const auto path =
        worlds_.world_path(world_id) / "scenes" / (scene_id + ".json");
    auto json = read_json(path);
    auto narrative = json.at("narrative").get<std::string>();
    if (!narrative.empty() && !markdown.empty()) {
        narrative += "\n";
    }
    narrative += markdown;
    json["narrative"] = narrative;
    write_json(path, json);
    return scene_from_json(json);
}

TimelineEvent
NarrativeStore::record_timeline_event(const std::string& world_id,
                                      TimelineEvent event) {
    ensure_world_exists(worlds_, world_id);
    if (event.id.empty()) {
        event.id = make_id("event");
    }
    auto path = worlds_.world_path(world_id) / "timeline.json";
    auto timeline = read_json(path);
    if (!timeline.contains("events") || !timeline["events"].is_array()) {
        timeline["events"] = nlohmann::json::array();
    }
    timeline["events"].push_back(timeline_event_json(event));
    write_json(path, timeline);
    return event;
}

TimelineEvent NarrativeStore::advance_time(const std::string& world_id,
                                           TimelineEvent event) {
    return record_timeline_event(world_id, std::move(event));
}

std::vector<std::string>
NarrativeStore::insert_flashback_scene(const std::string& world_id,
                                       Scene scene) {
    ensure_world_exists(worlds_, world_id);
    std::vector<std::string> warnings;

    {
        SqliteDb db(database_path().string());
        Statement query(db, R"sql(
            SELECT id, world_time, participants
            FROM scenes
            WHERE world_id = ?1 AND world_time > ?2
            ORDER BY world_time ASC, id ASC
        )sql");
        bind_text(query, 1, world_id);
        bind_text(query, 2, scene.world_time);
        while (query.step()) {
            const auto existing_id = column_text(query, 0);
            const auto existing_time = column_text(query, 1);
            const auto participants =
                nlohmann::json::parse(column_text(query, 2))
                    .get<std::vector<std::string>>();
            if (overlaps(scene.participant_ids, participants)) {
                warnings.push_back("flashback " + scene.world_time +
                                   " precedes later scene " + existing_id +
                                   " at " + existing_time +
                                   " with overlapping participants");
            }
        }
    }

    auto created = create_scene(world_id, std::move(scene));

    const auto path =
        worlds_.world_path(world_id) / "scenes" / (created.id + ".json");
    auto json = read_json(path);
    json["is_flashback"] = true;
    write_json(path, json);

    SqliteDb db(database_path().string());
    Statement mark(db, R"sql(
        UPDATE scenes SET is_flashback = 1 WHERE world_id = ?1 AND id = ?2
    )sql");
    bind_text(mark, 1, world_id);
    bind_text(mark, 2, created.id);
    execute_bound(mark);

    return warnings;
}

ChapterContext
NarrativeStore::chapter_context(const std::string& world_id,
                                const std::string& chapter_id) const {
    if (!worlds_.get_world(world_id).has_value()) {
        throw std::runtime_error("unknown world: " + world_id);
    }

    const auto chapter_path =
        worlds_.world_path(world_id) / "chapters" / (chapter_id + ".json");
    const auto chapter = chapter_from_json(read_json(chapter_path));

    ChapterContext context;
    context.chapter_pitch = chapter.pitch;
    context.emotional_curve_position = chapter.emotional_curve;

    if (chapter.arc_id.has_value()) {
        const auto arc_path =
            worlds_.world_path(world_id) / "arcs" / (*chapter.arc_id + ".json");
        context.arc_purpose = arc_from_json(read_json(arc_path)).purpose;
    }

    std::set<std::string> paid(chapter.foreshadowing_paid.begin(),
                               chapter.foreshadowing_paid.end());
    for (const auto& id : chapter.foreshadowing_planted) {
        if (!paid.contains(id)) {
            context.open_foreshadowing_ids.push_back(id);
        }
    }

    SqliteDb db(database_path().string());
    Statement query(db, R"sql(
        SELECT s.id
        FROM scenes s
        JOIN chapters c ON c.id = s.chapter_id
        WHERE s.world_id = ?1
          AND s.status = 'completed'
          AND c.number < ?2
        ORDER BY c.number ASC, s.world_time ASC, s.id ASC
    )sql");
    bind_text(query, 1, world_id);
    bind_int(query, 2, chapter.number);
    while (query.step()) {
        const auto scene_id = column_text(query, 0);
        const auto scene_path =
            worlds_.world_path(world_id) / "scenes" / (scene_id + ".json");
        const auto summary = first_line_summary(
            scene_from_json(read_json(scene_path)).narrative);
        if (!summary.empty()) {
            context.previous_scene_summaries.push_back(summary);
        }
    }

    return context;
}

} // namespace merak::worldbuilding
