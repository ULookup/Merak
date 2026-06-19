#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/pg_helpers.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace merak::worldbuilding {

WorldbuildingService::WorldbuildingService(std::string_view pg_conninfo,
                                           std::filesystem::path root,
                                           std::unique_ptr<merak::kg::KnowledgeGraphProvider> kg_provider)
    : root_(std::move(root)),
      worlds_(pg_conninfo, root_),
      agents_(worlds_, pg_conninfo, root_),
      narrative_(worlds_, pg_conninfo, root_),
      foreshadowing_(worlds_, narrative_, pg_conninfo, root_),
      secrets_(worlds_, foreshadowing_, pg_conninfo, root_),
      voice_(),
      kg_provider_(std::move(kg_provider)),
      orchestrator_(worlds_, agents_, narrative_, foreshadowing_, secrets_, voice_, kg_provider_.get()),
      pending_pool_(std::make_unique<PgPool>(pg_conninfo, 2)) {}

WorldbuildingService::~WorldbuildingService() = default;

void WorldbuildingService::initialize() {
    worlds_.initialize();
    ensure_pending_creation_table();
}

void WorldbuildingService::ensure_pending_creation_table() {
    PgConn conn(*pending_pool_);
    conn.exec(R"(
        CREATE TABLE IF NOT EXISTS pending_creations (
            creation_id TEXT PRIMARY KEY,
            world_id TEXT NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
            tool_name TEXT NOT NULL,
            params JSONB NOT NULL DEFAULT '{}',
            preview JSONB NOT NULL DEFAULT '{}',
            created_at TEXT NOT NULL
        )
    )");
    conn.exec("CREATE INDEX IF NOT EXISTS idx_pending_creations_world ON pending_creations(world_id)");
}

void WorldbuildingService::persist_pending_creation(const PendingCreation& pc) {
    PgConn conn(*pending_pool_);
    conn.execute(R"(
        INSERT INTO pending_creations (creation_id, world_id, tool_name, params, preview, created_at)
        VALUES ($1, $2, $3, $4::jsonb, $5::jsonb, $6)
        ON CONFLICT (creation_id) DO UPDATE SET
            world_id = EXCLUDED.world_id,
            tool_name = EXCLUDED.tool_name,
            params = EXCLUDED.params,
            preview = EXCLUDED.preview,
            created_at = EXCLUDED.created_at
    )", {pc.creation_id, pc.world_id, pc.tool_name, pc.params.dump(),
          pc.preview.dump(), now_iso_utc()});
}

std::optional<PendingCreation> WorldbuildingService::load_pending_creation(
        const std::string& creation_id) const {
    PgConn conn(*pending_pool_);
    auto rows = conn.query(R"(
        SELECT creation_id, world_id, tool_name, params::text, preview::text
        FROM pending_creations WHERE creation_id = $1
    )", {creation_id});
    if (rows.ntuples() == 0) return std::nullopt;

    PendingCreation pc;
    pc.creation_id = rows.get(0, 0);
    pc.world_id = rows.get(0, 1);
    pc.tool_name = rows.get(0, 2);
    pc.params = nlohmann::json::parse(rows.get(0, 3), nullptr, false);
    if (pc.params.is_discarded()) pc.params = nlohmann::json::object();
    pc.preview = nlohmann::json::parse(rows.get(0, 4), nullptr, false);
    if (pc.preview.is_discarded()) pc.preview = nlohmann::json::object();
    pc.created_at = std::chrono::steady_clock::now();
    return pc;
}

void WorldbuildingService::delete_pending_creation(const std::string& creation_id) {
    PgConn conn(*pending_pool_);
    conn.execute("DELETE FROM pending_creations WHERE creation_id = $1", {creation_id});
}

WorldMeta WorldbuildingService::create_world(std::string name,
                                              std::string description) {
    return worlds_.create_world(std::move(name), std::move(description));
}

WorldMeta WorldbuildingService::update_world(const std::string& world_id,
                                              const std::optional<std::string>& name,
                                              const std::optional<std::string>& description) {
    return worlds_.update_world(world_id, name, description);
}

std::vector<WorldMeta> WorldbuildingService::list_worlds() const {
    return worlds_.list_worlds();
}

AgentRecord
WorldbuildingService::create_character(const std::string& world_id,
                                        CharacterCard card) {
    auto agent = agents_.create_character(world_id, std::move(card));
    sync_entity_to_kg({agent.name, merak::kg::EntityType::Agent,
                       agent.id, agent.world_id, agent.created_at});
    return agent;
}

AgentRecord
WorldbuildingService::create_manager(const std::string& world_id,
                                      AgentKind kind,
                                      std::string name,
                                      std::string instructions) {
    return agents_.create_manager(world_id, kind, std::move(name),
                                   std::move(instructions));
}

AgentRecord
WorldbuildingService::create_group(const std::string& world_id,
                                    std::string name,
                                    std::string culture_card,
                                    std::vector<std::string> members) {
    auto agent = agents_.create_group(world_id, std::move(name),
                                     std::move(culture_card), std::move(members));
    sync_entity_to_kg({agent.name, merak::kg::EntityType::Organization,
                       agent.id, agent.world_id, agent.created_at});
    return agent;
}

Chapter WorldbuildingService::create_chapter(const std::string& world_id,
                                              Chapter chapter) {
    return narrative_.create_chapter(world_id, std::move(chapter));
}

Arc WorldbuildingService::create_arc(const std::string& world_id, Arc arc) {
    return narrative_.create_arc(world_id, std::move(arc));
}

Scene WorldbuildingService::create_scene(const std::string& world_id,
                                          Scene scene) {
    return narrative_.create_scene(world_id, std::move(scene));
}

ScenePreparation
WorldbuildingService::prepare_scene(const std::string& world_id,
                                     const std::string& scene_id) {
    return orchestrator_.prepare_scene(world_id, scene_id, *this);
}

SceneWrapUp WorldbuildingService::end_scene(const std::string& world_id,
                                              const std::string& scene_id,
                                              const std::string& final_markdown) {
    auto wrapup = orchestrator_.finish_scene(world_id, scene_id, final_markdown);

    if (entity_event_handler_) {
        entity_event_handler_("scene_ended", world_id, {
            {"scene_id", scene_id},
            {"pending_diary_agents", wrapup.pending_diary_agents}
        });
    }

    return wrapup;
}

void WorldbuildingService::notify_diary_created(const std::string& world_id,
                                                  const std::string& diary_id,
                                                  const std::string& agent_id,
                                                  const std::string& scene_id,
                                                  const std::string& mood,
                                                  int leak_risk_level) {
    if (entity_event_handler_) {
        entity_event_handler_("diary_created", world_id, {
            {"diary_id", diary_id},
            {"agent_id", agent_id},
            {"scene_id", scene_id},
            {"mood", mood},
            {"leak_risk_level", leak_risk_level}
        });
    }
}

void WorldbuildingService::notify_memory_summary_created(const std::string& world_id,
                                                          const std::string& summary_id,
                                                          const std::string& agent_id,
                                                          const std::vector<std::string>& source_diary_ids) {
    if (entity_event_handler_) {
        entity_event_handler_("memory_summary_created", world_id, {
            {"summary_id", summary_id},
            {"agent_id", agent_id},
            {"source_diary_ids", source_diary_ids}
        });
    }
}

Foreshadowing
WorldbuildingService::plant_foreshadowing(const std::string& world_id,
                                           Foreshadowing item) {
    return foreshadowing_.plant(world_id, std::move(item));
}

Secret WorldbuildingService::create_secret(const std::string& world_id,
                                            Secret secret) {
    return secrets_.create(world_id, std::move(secret));
}

std::vector<VoiceComparison>
WorldbuildingService::voice_check(const std::string& world_id) const {
    std::unordered_set<std::string> world_agent_ids;
    for (const auto& agent : worlds_.list_agents(world_id)) {
        world_agent_ids.insert(agent.id);
    }

    std::vector<VoiceFingerprint> scoped;
    for (const auto& fp : voice_.list_fingerprints()) {
        if (world_agent_ids.contains(fp.agent_id)) {
            scoped.push_back(fp);
        }
    }
    return voice_.check_all(scoped);
}

// Preview builders (no DB write)

nlohmann::json WorldbuildingService::build_scene_preview(const std::string& world_id,
                                                         const nlohmann::json& params) {
    nlohmann::json preview;
    preview["title"] = params.value("title", "");
    preview["chapter_id"] = params.value("chapter_id", "");
    preview["world_time"] = params.value("world_time", "");
    preview["narrative"] = params.value("narrative", "");
    preview["participant_ids"] = params.value("participant_ids", nlohmann::json::array());
    if (params.contains("location_id")) preview["location_id"] = params["location_id"];
    if (params.contains("section_id")) preview["section_id"] = params["section_id"];
    return preview;
}

nlohmann::json WorldbuildingService::build_chapter_preview(const std::string& world_id,
                                                           const nlohmann::json& params) {
    nlohmann::json preview;
    preview["title"] = params.value("title", "");
    if (params.contains("arc_id")) preview["arc_id"] = params["arc_id"];
    preview["number"] = params.value("number", params.value("order_index", 0));
    preview["pitch"] = params.value("pitch", params.value("summary", ""));
    return preview;
}

nlohmann::json WorldbuildingService::build_arc_preview(const std::string& world_id,
                                                       const nlohmann::json& params) {
    nlohmann::json preview;
    preview["title"] = params.value("title", params.value("name", ""));
    preview["purpose"] = params.value("purpose", params.value("description", ""));
    preview["chapter_numbers"] = params.value("chapter_numbers",
                                              params.value("chapter_ids", nlohmann::json::array()));
    return preview;
}

nlohmann::json WorldbuildingService::build_secret_preview(const std::string& world_id,
                                                          const nlohmann::json& params) {
    nlohmann::json preview;
    preview["truth"] = params.value("truth", params.value("content", ""));
    preview["holder_id"] = params.value("holder_id", "");
    preview["stakes"] = params.value("stakes", "");
    if (!params.contains("holder_id") && params.contains("holder_agent_ids")
        && params["holder_agent_ids"].is_array() && !params["holder_agent_ids"].empty()) {
        preview["holder_id"] = params["holder_agent_ids"][0];
    }
    return preview;
}

nlohmann::json WorldbuildingService::build_world_knowledge_preview(const std::string& world_id,
                                                                    const nlohmann::json& params) {
    nlohmann::json preview;
    preview["category"] = params.value("category", "other");
    preview["content"] = params.value("content", "");
    preview["tags"] = params.value("tags", nlohmann::json::array());
    if (params.contains("related_ids")) preview["related_ids"] = params["related_ids"];
    else if (params.contains("related_entity_ids"))
        preview["related_ids"] = params["related_entity_ids"];
    else preview["related_ids"] = nlohmann::json::array();
    return preview;
}

nlohmann::json WorldbuildingService::build_location_preview(const std::string& world_id,
                                                            const nlohmann::json& params) {
    nlohmann::json preview;
    preview["name"] = params.value("name", "");
    preview["description"] = params.value("description", "");
    preview["region"] = params.value("region", "");
    if (params.contains("parent_location_id"))
        preview["parent_location_id"] = params["parent_location_id"];
    return preview;
}

// Store and retrieve pending creations

std::string WorldbuildingService::store_pending_creation(
        const std::string& world_id, const std::string& tool_name,
        const nlohmann::json& params, const nlohmann::json& preview) {
    PendingCreation pc;
    pc.creation_id = "creation_" + make_id("c");
    pc.tool_name = tool_name;
    pc.world_id = world_id;
    pc.params = params;
    pc.preview = preview;
    pc.created_at = std::chrono::steady_clock::now();
    persist_pending_creation(pc);
    {
        std::lock_guard lock(pending_mutex_);
        pending_creations_[pc.creation_id] = pc;
    }
    return pc.creation_id;
}

std::optional<PendingCreation> WorldbuildingService::get_pending_creation(
        const std::string& creation_id) const {
    {
        std::lock_guard lock(pending_mutex_);
        auto it = pending_creations_.find(creation_id);
        if (it != pending_creations_.end()) return it->second;
    }

    auto loaded = load_pending_creation(creation_id);
    if (loaded) {
        std::lock_guard lock(pending_mutex_);
        pending_creations_[creation_id] = *loaded;
    }
    return loaded;
}

// Resolution (writes to DB on allow/modify)

nlohmann::json WorldbuildingService::resolve_creation(
        const std::string& creation_id,
        const std::string& decision,
        const nlohmann::json& modifications) {

    PendingCreation pc;
    {
        std::lock_guard lock(pending_mutex_);
        auto it = pending_creations_.find(creation_id);
        if (it != pending_creations_.end()) {
            pc = std::move(it->second);
            pending_creations_.erase(it);
        }
    }

    if (pc.creation_id.empty()) {
        auto loaded = load_pending_creation(creation_id);
        if (!loaded) {
            throw std::runtime_error("Pending creation not found: " + creation_id);
        }
        pc = std::move(*loaded);
    }

    if (decision == "deny") {
        delete_pending_creation(creation_id);
        return {{"ok", true}, {"decision", "deny"}, {"creation_id", creation_id}};
    }

    delete_pending_creation(creation_id);
    // Step 2: Merge modifications for "modify"
    nlohmann::json final_params = pc.params;
    if (decision == "modify" && !modifications.is_null()) {
        for (auto& [key, value] : modifications.items()) {
            final_params[key] = value;
        }
    }

    // Step 3: Write to DB (no lock held)
    nlohmann::json result;
    result["creation_id"] = creation_id;
    result["decision"] = decision;

    try {
        // Construct entity from final_params and call the appropriate store method
        if (pc.tool_name == "create_scene") {
            Scene scene;
            scene.title = final_params.value("title", "");
            scene.chapter_id = final_params.value("chapter_id", "");
            scene.world_time = final_params.value("world_time", "");
            scene.narrative = final_params.value("narrative", "");
            scene.section_id = final_params.value("section_id", "");
            scene.location_id = final_params.value("location_id", "");
            if (final_params.contains("participant_ids") && final_params["participant_ids"].is_array()) {
                for (auto& pid : final_params["participant_ids"])
                    if (pid.is_string())
                        scene.participant_ids.push_back(pid.get<std::string>());
            }
            scene.status = SceneStatus::Draft;

            auto created = narrative().create_scene(pc.world_id, std::move(scene));
            result["scene_id"] = created.id;

        } else if (pc.tool_name == "create_chapter") {
            Chapter chapter;
            chapter.title = final_params.value("title", "");
            chapter.pitch = final_params.value("pitch", final_params.value("summary", ""));
            chapter.number = final_params.value("number", final_params.value("order_index", 0));
            chapter.arc_id = final_params.value("arc_id", "");
            chapter.status = ChapterStatus::Outline;
            chapter.emotional_curve = nlohmann::json::array();

            auto created = narrative().create_chapter(pc.world_id, std::move(chapter));
            result["chapter_id"] = created.id;

        } else if (pc.tool_name == "create_arc") {
            Arc arc;
            arc.title = final_params.value("title", final_params.value("name", ""));
            arc.purpose = final_params.value("purpose", final_params.value("description", ""));
            if (final_params.contains("chapter_numbers") && final_params["chapter_numbers"].is_array()) {
                for (auto& cn : final_params["chapter_numbers"]) {
                    if (cn.is_number()) arc.chapter_numbers.push_back(cn.get<int>());
                }
            } else if (final_params.contains("chapter_ids") && final_params["chapter_ids"].is_array()) {
                for (auto& cn : final_params["chapter_ids"]) {
                    if (cn.is_number()) arc.chapter_numbers.push_back(cn.get<int>());
                }
            }
            arc.climax_scene_id = final_params.value("climax_scene_id", "");

            auto created = narrative().create_arc(pc.world_id, std::move(arc));
            result["arc_id"] = created.id;

        } else if (pc.tool_name == "create_secret") {
            Secret secret;
            secret.truth = final_params.value("truth", final_params.value("content", ""));
            secret.holder_id = final_params.value("holder_id", "");
            secret.stakes = final_params.value("stakes", "");
            if (!final_params.contains("holder_id") && final_params.contains("holder_agent_ids")
                && final_params["holder_agent_ids"].is_array()
                && !final_params["holder_agent_ids"].empty()) {
                if (final_params["holder_agent_ids"][0].is_string())
                    secret.holder_id = final_params["holder_agent_ids"][0].get<std::string>();
            }
            secret.public_version = final_params.value("public_version", "");
            secret.status = SecretStatus::Active;
            secret.believed_truths = nlohmann::json::object();

            auto created = secrets().create(pc.world_id, std::move(secret));
            result["secret_id"] = created.id;

        } else if (pc.tool_name == "add_world_knowledge") {
            WorldKnowledge wk;
            wk.id = make_id("wk");
            wk.category = final_params.value("category", "other");
            wk.content = final_params.value("content", "");
            if (final_params.contains("tags") && final_params["tags"].is_array()) {
                for (auto& t : final_params["tags"])
                    if (t.is_string())
                        wk.tags.push_back(t.get<std::string>());
            }
            if (final_params.contains("related_ids") && final_params["related_ids"].is_array()) {
                for (auto& rid : final_params["related_ids"])
                    if (rid.is_string())
                        wk.related_ids.push_back(rid.get<std::string>());
            } else if (final_params.contains("related_entity_ids")
                       && final_params["related_entity_ids"].is_array()) {
                for (auto& rid : final_params["related_entity_ids"])
                    if (rid.is_string())
                        wk.related_ids.push_back(rid.get<std::string>());
            }

            worlds().add_world_knowledge(pc.world_id, wk);
            result["knowledge_id"] = wk.id;

        } else if (pc.tool_name == "create_location") {
            Location loc;
            loc.name = final_params.value("name", "");
            loc.description = final_params.value("description", "");
            loc.region = final_params.value("region", "");
            loc.parent_location_id = final_params.value("parent_location_id", "");

            auto created = worlds().add_location(pc.world_id, std::move(loc));
            result["location_id"] = created.id;
            sync_entity_to_kg({created.name, merak::kg::EntityType::Location,
                               created.id, pc.world_id, created.created_at});

        } else {
            throw std::runtime_error("Unknown tool_name: " + pc.tool_name);
        }
    } catch (...) {
        // Step 4: On failure, re-insert pc into map and PostgreSQL for retry.
        persist_pending_creation(pc);
        std::lock_guard lock(pending_mutex_);
        pending_creations_[creation_id] = std::move(pc);
        throw;
    }

    return result;
}

void WorldbuildingService::sync_entity_to_kg(const merak::kg::GraphEntity& entity) {
    if (!kg_provider_) {
        spdlog::warn("sync_entity_to_kg: kg_provider not available, skipping entity '{}'",
                     entity.name);
        return;
    }
    kg_provider_->upsert_entity(entity);
}

std::string WorldbuildingService::build_world_context(const std::string& world_id) const {
    std::ostringstream ctx;
    auto world = worlds_.get_world(world_id);
    if (!world) return "";

    ctx << "世界: " << world->name << "\n";
    if (!world->description.empty()) {
        ctx << "描述: " << world->description << "\n";
    }

    auto knowledge = worlds_.get_world_knowledge(world_id, "");
    if (!knowledge.empty()) {
        ctx << "\n世界观知识:\n";
        for (const auto& wk : knowledge) {
            ctx << "- [" << wk.category << "] " << wk.content << "\n";
        }
    }

    auto agents = worlds_.list_agents(world_id);
    if (!agents.empty()) {
        ctx << "\n角色:\n";
        for (const auto& a : agents) {
            ctx << "- " << a.name << " (" << to_string(a.kind) << ")\n";
        }
    }

    auto locations = worlds_.list_locations(world_id);
    if (!locations.empty()) {
        ctx << "\n地点:\n";
        for (const auto& loc : locations) {
            ctx << "- " << loc.name;
            if (!loc.region.empty()) ctx << " [" << loc.region << "]";
            ctx << "\n";
        }
    }

    return ctx.str();
}

WorldbuildingService::ChapterReview
WorldbuildingService::get_chapter_review(const std::string& world_id,
                                          const std::string& chapter_id) const {
    ChapterReview review;
    review.chapter_id = chapter_id;

    auto chapter = narrative_.get_chapter(world_id, chapter_id);
    if (!chapter) {
        throw std::runtime_error("Chapter not found: " + chapter_id);
    }
    review.title = chapter->title;

    // Collect scenes for this chapter
    auto scenes = narrative_.list_scenes(world_id, chapter_id);
    std::unordered_set<std::string> seen_characters;
    for (const auto& ss : scenes) {
        auto scene = narrative_.get_scene(world_id, ss.id);
        if (!scene) continue;

        // Count Chinese characters in narrative text
        int chinese_chars = 0;
        for (size_t i = 0; i < scene->narrative.length(); ) {
            unsigned char c = static_cast<unsigned char>(scene->narrative[i]);
            int len = 1;
            if ((c & 0x80) == 0x00) {
                len = 1;
            } else if ((c & 0xE0) == 0xC0) {
                len = 2;
            } else if ((c & 0xF0) == 0xE0) {
                len = 3;
            } else if ((c & 0xF8) == 0xF0) {
                len = 4;
            }
            if (len >= 3) chinese_chars++;  // CJK characters are 3+ bytes in UTF-8
            i += len;
        }
        review.word_count += chinese_chars;

        // Collect character names from participant_ids
        for (const auto& pid : scene->participant_ids) {
            if (seen_characters.contains(pid)) continue;
            seen_characters.insert(pid);
            auto agent = agents_.get_agent(pid);
            if (agent) {
                review.character_names.push_back(agent->name);
            }
        }
    }

    // Collect foreshadowings planted/paid in this chapter
    auto all_foreshadows = foreshadowing_.list(world_id, std::nullopt);
    for (const auto& f : all_foreshadows) {
        if (f.planted_at.has_value() && *f.planted_at == chapter_id) {
            review.foreshadowing_planted.push_back({f.id, f.content});
        }
        if (f.paid_at.has_value() && *f.paid_at == chapter_id) {
            review.foreshadowing_paid.push_back({f.id, f.content});
        }
    }

    // Generate writing advice in Chinese
    int char_count = static_cast<int>(review.character_names.size());
    int foreshadow_open = static_cast<int>(review.foreshadowing_planted.size());
    int foreshadow_paid = static_cast<int>(review.foreshadowing_paid.size());

    std::ostringstream advice;
    advice << "本章出场" << char_count << "个角色，"
           << "埋下伏笔" << foreshadow_open << "个，回收伏笔" << foreshadow_paid << "个。";

    if (review.word_count < 1000) {
        advice << "篇幅较短，可以考虑适当展开场景描写或角色对话。";
    } else if (review.word_count > 5000) {
        advice << "篇幅较长，建议保持节奏感，避免读者疲劳。";
    } else {
        advice << "篇幅适中，节奏良好。";
    }

    if (foreshadow_open == 0 && foreshadow_paid == 0) {
        advice << "本章暂无伏笔操作，可以考虑为后续情节埋下线索。";
    }

    if (char_count > 0) {
        advice << "主要角色包括：";
        for (size_t i = 0; i < review.character_names.size() && i < 5; i++) {
            if (i > 0) advice << "、";
            advice << review.character_names[i];
        }
        if (review.character_names.size() > 5) {
            advice << "等" << review.character_names.size() << "人";
        }
        advice << "。";
    }

    review.writing_advice = advice.str();
    return review;
}

WorldbuildingService::ExportResult
WorldbuildingService::export_chapters(const std::string& world_id,
                                      const std::vector<std::string>& chapter_ids,
                                      const std::string& title,
                                      const std::string& author) {
    if (chapter_ids.empty()) {
        throw std::runtime_error("chapter_ids must not be empty");
    }

    auto world = worlds_.get_world(world_id);
    if (!world) {
        throw std::runtime_error("World not found: " + world_id);
    }

    // Build output directory: root_/exports/<world_id>/
    // Use world->id (not name) to prevent path traversal
    auto export_dir = root_ / "exports" / world->id;
    std::filesystem::create_directories(export_dir);

    // Build filename from title
    std::string filename = title.empty() ? "export" : title;
    for (auto& ch : filename) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?'
            || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            ch = '_';
        }
    }
    auto file_path = export_dir / (filename + ".txt");

    std::ofstream out(file_path);
    if (!out) {
        spdlog::error("export_chapters: failed to open file: {}", file_path.string());
        throw std::runtime_error("Failed to open output file");
    }

    int total_chars = 0;

    // Title and author header
    if (!title.empty()) {
        out << title << "\n";
        total_chars += static_cast<int>(title.length());
    }
    if (!author.empty()) {
        out << author << "\n";
        total_chars += static_cast<int>(author.length());
    }
    out << "\n";

    for (const auto& cid : chapter_ids) {
        auto chapter = narrative_.get_chapter(world_id, cid);
        if (!chapter) {
            spdlog::warn("export_chapters: chapter not found: {}", cid);
            continue;
        }

        out << "# " << chapter->title << "\n\n";
        total_chars += static_cast<int>(chapter->title.length());

        auto scenes = narrative_.list_scenes(world_id, cid);
        for (const auto& ss : scenes) {
            auto scene = narrative_.get_scene(world_id, ss.id);
            if (!scene) continue;

            out << "## " << scene->title << "\n\n";
            total_chars += static_cast<int>(scene->title.length());

            out << scene->narrative << "\n\n";
            total_chars += static_cast<int>(scene->narrative.length());
        }

        out << "---\n";
    }

    out.flush();
    if (out.fail()) {
        spdlog::error("export_chapters: failed to write file: {}", file_path.string());
        throw std::runtime_error("Failed to write output file");
    }
    out.close();

    return {file_path.string(), total_chars};
}

nlohmann::json WorldbuildingService::get_dashboard(const std::string& world_id) const {
    auto world = worlds_.get_world(world_id);
    if (!world) {
        throw std::runtime_error("World not found: " + world_id);
    }

    nlohmann::json dash;

    // World info
    dash["world"] = {
        {"id", world->id},
        {"name", world->name},
        {"description", world->description},
        {"created_at", world->created_at},
        {"updated_at", world->updated_at}
    };

    // Agent counts by kind
    auto agents = worlds_.list_agents(world_id);
    std::map<std::string, int> agent_by_kind;
    for (const auto& a : agents) {
        agent_by_kind[to_string(a.kind)]++;
    }
    dash["agents"] = {
        {"total", agents.size()},
        {"by_kind", agent_by_kind}
    };

    // Chapters
    auto chapters = narrative_.list_chapters(world_id);
    int chapters_completed = 0;
    int chapters_drafting = 0;
    int chapters_outline = 0;
    int chapters_revised = 0;
    for (const auto& ch : chapters) {
        if (ch.status == "completed") ++chapters_completed;
        else if (ch.status == "drafting") ++chapters_drafting;
        else if (ch.status == "outline") ++chapters_outline;
        else if (ch.status == "revised") ++chapters_revised;
    }
    dash["chapters"] = {
        {"total", chapters.size()},
        {"completed", chapters_completed},
        {"drafting", chapters_drafting},
        {"outline", chapters_outline},
        {"revised", chapters_revised},
    };

    // Scenes
    auto scenes = narrative_.list_scenes(world_id);
    int scenes_completed = 0;
    int scenes_writing = 0;
    int scenes_draft = 0;
    for (const auto& s : scenes) {
        if (s.status == "completed") ++scenes_completed;
        else if (s.status == "writing") ++scenes_writing;
        else if (s.status == "draft") ++scenes_draft;
    }
    dash["scenes"] = {
        {"total", scenes.size()},
        {"completed", scenes_completed},
        {"writing", scenes_writing},
        {"draft", scenes_draft},
    };

    // Arcs
    auto arcs = narrative_.list_arcs(world_id);
    dash["arcs"] = {
        {"total", arcs.size()},
        {"items", nlohmann::json::array()}
    };
    for (const auto& a : arcs) {
        dash["arcs"]["items"].push_back({
            {"id", a.id}, {"title", a.title}, {"purpose", a.purpose},
            {"status", a.status}
        });
    }

    // Locations
    auto locations = worlds_.list_locations(world_id);
    dash["locations"] = {{"total", locations.size()}};

    // Knowledge items
    auto knowledge = worlds_.get_world_knowledge(world_id, "");
    std::map<std::string, int> knowledge_by_category;
    for (const auto& k : knowledge) {
        knowledge_by_category[k.category]++;
    }
    dash["knowledge"] = {
        {"total", knowledge.size()},
        {"by_category", knowledge_by_category}
    };

    // Factions
    auto factions = worlds_.list_factions(world_id);
    dash["factions"] = {{"total", factions.size()}};

    // Secrets
    auto active_secrets = secrets_.list(world_id, SecretStatus::Active);
    auto exposed_secrets = secrets_.list(world_id, SecretStatus::Exposed);
    auto abandoned_secrets = secrets_.list(world_id, SecretStatus::Abandoned);
    dash["secrets"] = {
        {"active", active_secrets.size()},
        {"exposed", exposed_secrets.size()},
        {"abandoned", abandoned_secrets.size()},
        {"total", active_secrets.size() + exposed_secrets.size() + abandoned_secrets.size()}
    };

    // Foreshadowing stats
    auto fs_stats = foreshadowing_.stats(world_id);
    dash["foreshadowing"] = {
        {"open", fs_stats.open},
        {"paid", fs_stats.paid},
        {"abandoned", fs_stats.abandoned}
    };

    // File links
    auto file_links = worlds_.list_file_links(world_id);
    dash["file_links"] = {{"total", file_links.is_array() ? file_links.size() : 0}};

    // Reminders — open foreshadowings that need attention
    auto open_fs = foreshadowing_.list(world_id, ForeshadowStatus::Open);
    nlohmann::json reminders = nlohmann::json::array();
    for (const auto& fs : open_fs) {
        reminders.push_back({
            {"type", "foreshadowing"},
            {"id", fs.id},
            {"content", fs.content},
            {"pay_off_idea", fs.pay_off_idea}
        });
    }
    // Active secrets as reminders
    for (const auto& s : active_secrets) {
        reminders.push_back({
            {"type", "secret"},
            {"id", s.id},
            {"holder_id", s.holder_id},
            {"truth", s.truth},
            {"stakes", s.stakes}
        });
    }
    dash["reminders"] = reminders;

    // Completion percentages
    double chapter_pct = chapters.empty() ? 0.0 :
        (100.0 * (chapters_completed + chapters_revised) / chapters.size());
    double scene_pct = scenes.empty() ? 0.0 :
        (100.0 * scenes_completed / scenes.size());
    double fs_pct = (fs_stats.open + fs_stats.paid + fs_stats.abandoned) == 0 ? 0.0 :
        (100.0 * fs_stats.paid / (fs_stats.open + fs_stats.paid + fs_stats.abandoned));
    double secret_resolved_pct = (active_secrets.size() + exposed_secrets.size() + abandoned_secrets.size()) == 0 ? 0.0 :
        (100.0 * exposed_secrets.size() / (active_secrets.size() + exposed_secrets.size() + abandoned_secrets.size()));

    dash["progress"] = {
        {"chapter_completion_pct", chapter_pct},
        {"scene_completion_pct", scene_pct},
        {"foreshadowing_paid_pct", fs_pct},
        {"secret_exposed_pct", secret_resolved_pct}
    };

    return dash;
}
WorldbuildingService::WorldSnapshot
WorldbuildingService::export_world_snapshot(const std::string& world_id,
                                             bool include_diaries,
                                             bool include_memories) {
    auto world = worlds_.get_world(world_id);
    if (!world) {
        throw std::runtime_error("World not found: " + world_id);
    }

    WorldSnapshot snap;
    snap.schema_version = "1.0";
    snap.snapshot_id = make_id("snap");
    snap.exported_at = now_iso_utc();

    snap.source = {
        {"world_id", world->id},
        {"name", world->name},
        {"description", world->description}
    };

    nlohmann::json& p = snap.payload;

    // ── Agents ──
    auto agent_list = worlds_.list_agents(world_id);
    nlohmann::json agents_arr = nlohmann::json::array();
    for (const auto& a : agent_list) {
        nlohmann::json aj;
        aj["id"] = a.id;
        aj["world_id"] = a.world_id;
        aj["name"] = a.name;
        aj["display_name"] = a.display_name;
        aj["kind"] = to_string(a.kind);
        aj["created_at"] = a.created_at;
        aj["updated_at"] = a.updated_at;

        // CharacterCard
        auto card = agents_.load_character_card(a.id);
        nlohmann::json card_json;
        card_json["name"] = card.name;
        card_json["gender"] = card.gender;
        card_json["race"] = card.race;
        card_json["identity"] = card.identity;
        card_json["emotional_tendency"] = card.emotional_tendency;
        card_json["speaking_style"] = card.speaking_style;
        card_json["core_desire"] = card.core_desire;
        card_json["deep_fear"] = card.deep_fear;
        card_json["daily_goal"] = card.daily_goal;
        card_json["background"] = card.background;
        card_json["knowledge_scope"] = card.knowledge_scope;
        card_json["appearance"] = card.appearance;
        card_json["age"] = card.age;
        card_json["version"] = card.version;
        card_json["core_traits"] = card.core_traits;
        card_json["taboo_topics"] = card.taboo_topics;
        card_json["relations"] = card.relations;
        card_json["updated_at"] = card.updated_at;
        aj["card"] = card_json;

        // Diaries (if requested)
        if (include_diaries) {
            auto diaries = agents_.recent_diary(a.id, 1000);
            nlohmann::json diary_arr = nlohmann::json::array();
            for (const auto& d : diaries) {
                diary_arr.push_back({
                    {"id", d.id},
                    {"agent_id", d.agent_id},
                    {"scene_id", d.scene_id},
                    {"world_time", d.world_time},
                    {"content", d.content},
                    {"created_at", d.created_at},
                    {"mood", d.mood},
                    {"status", d.status},
                    {"leak_risk_level", d.leak_risk_level},
                    {"tokens_used", d.tokens_used}
                });
            }
            aj["diaries"] = diary_arr;
        }

        // Memory summaries (if requested)
        if (include_memories) {
            auto summaries = agents_.recent_summaries(a.id, 100);
            nlohmann::json summary_arr = nlohmann::json::array();
            for (const auto& s : summaries) {
                summary_arr.push_back({
                    {"id", s.id},
                    {"agent_id", s.agent_id},
                    {"period_start", s.period_start},
                    {"period_end", s.period_end},
                    {"summary", s.summary},
                    {"created_at", s.created_at},
                    {"source_diary_ids", s.source_diary_ids}
                });
            }
            aj["memory_summaries"] = summary_arr;
        }

        // Relations
        auto relations = agents_.relations_for(a.id);
        nlohmann::json rel_arr = nlohmann::json::array();
        for (const auto& r : relations) {
            rel_arr.push_back({
                {"agent_id", r.agent_id},
                {"target_id", r.target_id},
                {"relation_type", r.relation_type},
                {"description", r.description},
                {"intimacy", r.intimacy},
                {"key_events", r.key_events},
                {"updated_at", r.updated_at}
            });
        }
        aj["relations"] = rel_arr;

        agents_arr.push_back(aj);
    }
    p["agents"] = agents_arr;

    // ── Chapters with Scenes ──
    auto chapter_list = narrative_.list_chapters(world_id);
    nlohmann::json chapters_arr = nlohmann::json::array();
    for (const auto& ch_summary : chapter_list) {
        auto ch = narrative_.get_chapter(world_id, ch_summary.id);
        if (!ch) continue;
        nlohmann::json cj;
        cj["id"] = ch->id;
        cj["title"] = ch->title;
        cj["pitch"] = ch->pitch;
        cj["notes"] = ch->notes;
        cj["content"] = ch->content;
        cj["number"] = ch->number;
        cj["arc_id"] = ch->arc_id.has_value() ? nlohmann::json(*ch->arc_id) : nlohmann::json(nullptr);
        cj["status"] = to_string(ch->status);
        cj["emotional_curve"] = ch->emotional_curve;
        cj["scene_ids"] = ch->scene_ids;
        cj["foreshadowing_planted"] = ch->foreshadowing_planted;
        cj["foreshadowing_paid"] = ch->foreshadowing_paid;

        // Scenes for this chapter
        auto scene_list = narrative_.list_scenes(world_id, ch->id);
        nlohmann::json scenes_arr = nlohmann::json::array();
        for (const auto& s_summary : scene_list) {
            auto s = narrative_.get_scene(world_id, s_summary.id);
            if (!s) continue;
            nlohmann::json sj;
            sj["id"] = s->id;
            sj["title"] = s->title;
            sj["chapter_id"] = s->chapter_id;
            sj["world_time"] = s->world_time;
            sj["narrative"] = s->narrative;
            sj["section_id"] = s->section_id.has_value() ? nlohmann::json(*s->section_id) : nlohmann::json(nullptr);
            sj["location_id"] = s->location_id.has_value() ? nlohmann::json(*s->location_id) : nlohmann::json(nullptr);
            sj["participant_ids"] = s->participant_ids;
            sj["status"] = to_string(s->status);
            sj["pov_character_id"] = s->pov_character_id.has_value() ? nlohmann::json(*s->pov_character_id) : nlohmann::json(nullptr);
            sj["plot_goal"] = s->plot_goal;
            sj["emotional_goal"] = s->emotional_goal;
            sj["information_goal"] = s->information_goal;
            sj["external_conflict"] = s->external_conflict;
            sj["internal_conflict"] = s->internal_conflict;
            sj["hidden_conflict"] = s->hidden_conflict.has_value() ? nlohmann::json(*s->hidden_conflict) : nlohmann::json(nullptr);
            sj["foreshadowing_ids"] = s->foreshadowing_ids;
            sj["style_overrides"] = s->style_overrides;
            scenes_arr.push_back(sj);
        }
        cj["scenes"] = scenes_arr;
        chapters_arr.push_back(cj);
    }
    p["chapters"] = chapters_arr;

    // ── Arcs ──
    auto arc_list = narrative_.list_arcs(world_id);
    nlohmann::json arcs_arr = nlohmann::json::array();
    for (const auto& arc : arc_list) {
        arcs_arr.push_back({
            {"id", arc.id},
            {"title", arc.title},
            {"purpose", arc.purpose},
            {"status", arc.status},
            {"updated_at", arc.updated_at}
        });
    }
    p["arcs"] = arcs_arr;

    // ── Locations ──
    auto locations = worlds_.list_locations(world_id);
    nlohmann::json loc_arr = nlohmann::json::array();
    for (const auto& loc : locations) {
        nlohmann::json lj{
            {"id", loc.id},
            {"name", loc.name},
            {"description", loc.description},
            {"region", loc.region},
            {"created_at", loc.created_at}
        };
        lj["parent_location_id"] = loc.parent_location_id.has_value()
            ? nlohmann::json(*loc.parent_location_id) : nlohmann::json(nullptr);
        loc_arr.push_back(lj);
    }
    p["locations"] = loc_arr;

    // ── Factions ──
    auto factions = worlds_.list_factions(world_id);
    nlohmann::json fac_arr = nlohmann::json::array();
    for (const auto& f : factions) {
        fac_arr.push_back({
            {"id", f.id},
            {"world_id", f.world_id},
            {"name", f.name},
            {"description", f.description},
            {"goals", f.goals},
            {"member_agent_ids", f.member_agent_ids},
            {"rival_faction_ids", f.rival_faction_ids},
            {"created_at", f.created_at},
            {"updated_at", f.updated_at}
        });
    }
    p["factions"] = fac_arr;

    // ── Knowledge ──
    auto knowledge = worlds_.get_world_knowledge(world_id, "");
    nlohmann::json kn_arr = nlohmann::json::array();
    for (const auto& k : knowledge) {
        kn_arr.push_back({
            {"id", k.id},
            {"category", k.category},
            {"content", k.content},
            {"created_at", k.created_at},
            {"tags", k.tags},
            {"aliases", k.aliases},
            {"related_ids", k.related_ids}
        });
    }
    p["knowledge"] = kn_arr;

    // ── Foreshadowing ──
    auto foreshadowings = foreshadowing_.list(world_id, std::nullopt);
    nlohmann::json fs_arr = nlohmann::json::array();
    for (const auto& f : foreshadowings) {
        nlohmann::json fj{
            {"id", f.id},
            {"content", f.content},
            {"pay_off_idea", f.pay_off_idea},
            {"status", to_string(f.status)},
            {"hint_level", to_string(f.hint_level)},
            {"tags", f.tags},
            {"related_foreshadowing_ids", f.related_foreshadowing_ids},
            {"related_secret_ids", f.related_secret_ids},
            {"created_by", to_string(f.created_by)}
        };
        fj["planted_at"] = f.planted_at.has_value() ? nlohmann::json(*f.planted_at) : nlohmann::json(nullptr);
        fj["paid_at"] = f.paid_at.has_value() ? nlohmann::json(*f.paid_at) : nlohmann::json(nullptr);
        fs_arr.push_back(fj);
    }
    p["foreshadowing"] = fs_arr;

    // ── Secrets ──
    auto active_secrets = secrets_.list(world_id, SecretStatus::Active);
    auto exposed_secrets = secrets_.list(world_id, SecretStatus::Exposed);
    auto abandoned_secrets = secrets_.list(world_id, SecretStatus::Abandoned);
    nlohmann::json sec_arr = nlohmann::json::array();
    auto add_secret = [&](const auto& s) {
        nlohmann::json sj{
            {"id", s.id},
            {"holder_id", s.holder_id},
            {"truth", s.truth},
            {"public_version", s.public_version},
            {"stakes", s.stakes},
            {"aware_character_ids", s.aware_character_ids},
            {"suspicious_character_ids", s.suspicious_character_ids},
            {"related_foreshadowing_ids", s.related_foreshadowing_ids},
            {"believed_truths", s.believed_truths},
            {"status", to_string(s.status)}
        };
        sj["planted_at"] = s.planted_at.has_value() ? nlohmann::json(*s.planted_at) : nlohmann::json(nullptr);
        sj["exposed_at"] = s.exposed_at.has_value() ? nlohmann::json(*s.exposed_at) : nlohmann::json(nullptr);
        sec_arr.push_back(sj);
    };
    for (const auto& s : active_secrets) add_secret(s);
    for (const auto& s : exposed_secrets) add_secret(s);
    for (const auto& s : abandoned_secrets) add_secret(s);
    p["secrets"] = sec_arr;

    // ── Timeline ──
    {
        auto timeline_path = worlds_.world_path(world_id) / "timeline.json";
        nlohmann::json timeline_events = nlohmann::json::array();
        if (std::filesystem::exists(timeline_path)) {
            try {
                std::ifstream tf(timeline_path);
                if (tf) {
                    auto timeline_data = nlohmann::json::parse(tf);
                    if (timeline_data.contains("events") && timeline_data["events"].is_array()) {
                        timeline_events = timeline_data["events"];
                    }
                }
            } catch (...) {
                // If timeline file is corrupted, export empty array
                timeline_events = nlohmann::json::array();
            }
        }
        p["timeline"] = timeline_events;
    }

    // ── File links ──
    p["file_links"] = worlds_.list_file_links(world_id);

    // ── Manifest ──
    snap.manifest = {
        {"schema_version", snap.schema_version},
        {"exported_at", snap.exported_at},
        {"snapshot_id", snap.snapshot_id},
        {"agent_count", agents_arr.size()},
        {"chapter_count", chapters_arr.size()},
        {"arc_count", arcs_arr.size()},
        {"location_count", loc_arr.size()},
        {"faction_count", fac_arr.size()},
        {"knowledge_count", kn_arr.size()},
        {"foreshadowing_count", fs_arr.size()},
        {"secret_count", sec_arr.size()},
        {"timeline_event_count", p["timeline"].is_array() ? p["timeline"].size() : 0}
    };

    return snap;
}

WorldbuildingService::ImportResult
WorldbuildingService::import_snapshot(const nlohmann::json& snapshot,
                                       const std::optional<std::string>& target_name) {
    ImportResult result;
    auto& id_map = result.id_mapping;

    // Remap helpers
    auto remap = [&](const std::string& old_id) -> std::string {
        if (old_id.empty()) return old_id;
        auto it = id_map.find(old_id);
        return it != id_map.end() ? it->second : old_id;
    };

    auto remap_vec = [&](const std::vector<std::string>& ids) -> std::vector<std::string> {
        std::vector<std::string> out;
        for (const auto& id : ids) out.push_back(remap(id));
        return out;
    };

    auto remap_json_arr = [&](const nlohmann::json& arr) -> std::vector<std::string> {
        std::vector<std::string> out;
        if (arr.is_array()) {
            for (const auto& id : arr) {
                if (id.is_string()) out.push_back(remap(id.get<std::string>()));
            }
        }
        return out;
    };

    const auto& source = snapshot.value("source", nlohmann::json::object());
    const auto& payload = snapshot.value("payload", nlohmann::json::object());

    // ═══════════════════════════════════════════════════════════════
    // Step 1: Create world
    // ═══════════════════════════════════════════════════════════════
    std::string imported_name = source.value("name", "Imported World");
    std::string world_name = target_name.value_or(imported_name + " (imported)");
    std::string world_desc = source.value("description", "");
    auto world = worlds_.create_world(world_name, world_desc);
    result.world_id = world.id;

    std::string old_world_id = source.value("world_id", "");
    if (!old_world_id.empty()) id_map[old_world_id] = world.id;

    spdlog::info("import_snapshot: created world {} <- {}", world.id, old_world_id);

    // ═══════════════════════════════════════════════════════════════
    // Step 2: Create locations (two-pass for parent_location_id)
    // ═══════════════════════════════════════════════════════════════
    std::vector<std::pair<std::string, std::string>> loc_parent_fixup;

    if (payload.contains("locations") && payload["locations"].is_array()) {
        for (const auto& lj : payload["locations"]) {
            Location loc;
            loc.name = lj.value("name", "Unnamed Location");
            loc.description = lj.value("description", "");
            loc.region = lj.value("region", "");

            auto created = worlds_.add_location(world.id, std::move(loc));
            std::string old_id = lj.value("id", "");
            if (!old_id.empty()) id_map[old_id] = created.id;

            if (lj.contains("parent_location_id") && !lj["parent_location_id"].is_null()
                && lj["parent_location_id"].is_string()) {
                loc_parent_fixup.emplace_back(created.id, lj["parent_location_id"].get<std::string>());
            }
        }

        for (const auto& [new_id, old_parent] : loc_parent_fixup) {
            std::string new_parent = remap(old_parent);
            if (!new_parent.empty()) {
                worlds_.update_location(world.id, new_id,
                                        {{"parent_location_id", new_parent}});
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 3: Create individual agents + managers (groups deferred)
    // ═══════════════════════════════════════════════════════════════
    struct PendingGroup {
        nlohmann::json data;
        std::string old_id;
    };
    std::vector<PendingGroup> pending_groups;

    if (payload.contains("agents") && payload["agents"].is_array()) {
        for (const auto& aj : payload["agents"]) {
            std::string kind_str = aj.value("kind", "individual");
            std::string name = aj.value("name", "Unnamed");
            std::string old_id = aj.value("id", "");

            if (kind_str == "group") {
                pending_groups.push_back({aj, old_id});
                continue;
            }

            AgentRecord agent;
            // Determine if this is a manager kind (not individual/group)
            bool is_manager =
                kind_str == "god" || kind_str == "writer" ||
                kind_str == "map_manager" || kind_str == "history_manager" ||
                kind_str == "magic_system_manager" || kind_str == "faction_manager" ||
                kind_str == "relation_manager";

            if (!is_manager) {
                // Individual or unknown — create as character
                CharacterCard card;
                if (aj.contains("card") && aj["card"].is_object()) {
                    const auto& cj = aj["card"];
                    card.name = cj.value("name", name);
                    card.gender = cj.value("gender", "");
                    card.race = cj.value("race", "");
                    card.identity = cj.value("identity", "");
                    card.emotional_tendency = cj.value("emotional_tendency", "");
                    card.speaking_style = cj.value("speaking_style", "");
                    card.core_desire = cj.value("core_desire", "");
                    card.deep_fear = cj.value("deep_fear", "");
                    card.daily_goal = cj.value("daily_goal", "");
                    card.background = cj.value("background", "");
                    card.knowledge_scope = cj.value("knowledge_scope", "");
                    card.appearance = cj.value("appearance", "");
                    card.age = cj.value("age", 0);
                    card.version = cj.value("version", 1);
                    if (cj.contains("core_traits") && cj["core_traits"].is_array()) {
                        for (const auto& t : cj["core_traits"])
                            if (t.is_string()) card.core_traits.push_back(t.get<std::string>());
                    }
                    if (cj.contains("taboo_topics") && cj["taboo_topics"].is_array()) {
                        for (const auto& t : cj["taboo_topics"])
                            if (t.is_string()) card.taboo_topics.push_back(t.get<std::string>());
                    }
                    if (cj.contains("relations"))
                        card.relations = cj["relations"];
                } else {
                    card.name = name;
                }
                agent = agents_.create_character(world.id, std::move(card));
                sync_entity_to_kg({agent.name, merak::kg::EntityType::Agent,
                                   agent.id, agent.world_id, agent.created_at});
            } else {
                // Manager kind
                AgentKind kind = AgentKind::God;
                if (kind_str == "god") kind = AgentKind::God;
                else if (kind_str == "writer") kind = AgentKind::Writer;
                else if (kind_str == "map_manager") kind = AgentKind::MapManager;
                else if (kind_str == "history_manager") kind = AgentKind::HistoryManager;
                else if (kind_str == "magic_system_manager") kind = AgentKind::MagicSystemManager;
                else if (kind_str == "faction_manager") kind = AgentKind::FactionManager;
                else if (kind_str == "relation_manager") kind = AgentKind::RelationManager;

                agent = agents_.create_manager(world.id, kind, name, "");
            }

            if (!old_id.empty()) id_map[old_id] = agent.id;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 4: Create groups
    // ═══════════════════════════════════════════════════════════════
    for (auto& pg : pending_groups) {
        std::string name = pg.data.value("name", "Unnamed Group");
        std::string culture_card;
        if (pg.data.contains("card") && pg.data["card"].is_object()) {
            culture_card = pg.data["card"].value("background", "");
        }
        // Empty member list — group members are not serialized in the snapshot
        auto agent = agents_.create_group(world.id, name, culture_card, {});
        if (!pg.old_id.empty()) id_map[pg.old_id] = agent.id;
        sync_entity_to_kg({agent.name, merak::kg::EntityType::Organization,
                           agent.id, agent.world_id, agent.created_at});
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 5: Import relations
    // ═══════════════════════════════════════════════════════════════
    if (payload.contains("agents") && payload["agents"].is_array()) {
        for (const auto& aj : payload["agents"]) {
            std::string old_agent_id = aj.value("id", "");
            if (!aj.contains("relations") || !aj["relations"].is_array()) continue;

            for (const auto& rj : aj["relations"]) {
                RelationEntry rel;
                rel.agent_id = remap(rj.value("agent_id", ""));
                rel.target_id = remap(rj.value("target_id", ""));
                rel.relation_type = rj.value("relation_type", "");
                rel.description = rj.value("description", "");
                rel.intimacy = rj.value("intimacy", 0);
                if (rj.contains("key_events") && rj["key_events"].is_array()) {
                    for (const auto& ke : rj["key_events"])
                        if (ke.is_string()) rel.key_events.push_back(ke.get<std::string>());
                }
                if (!rel.agent_id.empty() && !rel.target_id.empty()) {
                    try {
                        agents_.upsert_relation(std::move(rel));
                    } catch (const std::exception& e) {
                        spdlog::warn("import_snapshot: failed to upsert relation {}->{}: {}",
                                     rel.agent_id, rel.target_id, e.what());
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 6: Import arcs
    // ═══════════════════════════════════════════════════════════════
    if (payload.contains("arcs") && payload["arcs"].is_array()) {
        for (const auto& aj : payload["arcs"]) {
            Arc arc;
            arc.title = aj.value("title", "");
            arc.purpose = aj.value("purpose", "");
            arc.status = aj.value("status", "");

            auto created = narrative_.create_arc(world.id, std::move(arc));
            std::string old_id = aj.value("id", "");
            if (!old_id.empty()) id_map[old_id] = created.id;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 7: Import chapters
    // ═══════════════════════════════════════════════════════════════
    struct ChapterSceneBatch {
        std::string new_chapter_id;
        nlohmann::json scenes_json;
    };
    std::vector<ChapterSceneBatch> chapter_scenes;

    if (payload.contains("chapters") && payload["chapters"].is_array()) {
        for (const auto& cj : payload["chapters"]) {
            Chapter ch;
            ch.title = cj.value("title", "Untitled Chapter");
            ch.pitch = cj.value("pitch", "");
            ch.notes = cj.value("notes", "");
            ch.content = cj.value("content", "");
            ch.number = cj.value("number", 0);

            // Remap arc_id
            if (cj.contains("arc_id") && !cj["arc_id"].is_null() && cj["arc_id"].is_string()) {
                ch.arc_id = remap(cj["arc_id"].get<std::string>());
            }

            // Parse status
            std::string status_str = cj.value("status", "outline");
            if (status_str == "drafting") ch.status = ChapterStatus::Drafting;
            else if (status_str == "completed") ch.status = ChapterStatus::Completed;
            else if (status_str == "revised") ch.status = ChapterStatus::Revised;
            else ch.status = ChapterStatus::Outline;

            if (cj.contains("emotional_curve"))
                ch.emotional_curve = cj["emotional_curve"];

            auto created = narrative_.create_chapter(world.id, std::move(ch));
            std::string old_id = cj.value("id", "");
            if (!old_id.empty()) id_map[old_id] = created.id;

            // Collect scenes for later creation
            if (cj.contains("scenes") && cj["scenes"].is_array()) {
                chapter_scenes.push_back({created.id, cj["scenes"]});
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 8: Import scenes
    // ═══════════════════════════════════════════════════════════════
    for (const auto& cs : chapter_scenes) {
        for (const auto& sj : cs.scenes_json) {
            Scene scene;
            scene.title = sj.value("title", "Untitled Scene");
            scene.chapter_id = cs.new_chapter_id;
            scene.world_time = sj.value("world_time", "");
            scene.narrative = sj.value("narrative", "");

            // Remap location_id
            if (sj.contains("location_id") && !sj["location_id"].is_null()
                && sj["location_id"].is_string()) {
                scene.location_id = remap(sj["location_id"].get<std::string>());
            }
            // Remap section_id
            if (sj.contains("section_id") && !sj["section_id"].is_null()
                && sj["section_id"].is_string()) {
                scene.section_id = remap(sj["section_id"].get<std::string>());
            }
            // Remap participant_ids
            scene.participant_ids = remap_json_arr(sj.value("participant_ids", nlohmann::json::array()));
            // Remap pov_character_id
            if (sj.contains("pov_character_id") && !sj["pov_character_id"].is_null()
                && sj["pov_character_id"].is_string()) {
                scene.pov_character_id = remap(sj["pov_character_id"].get<std::string>());
            }

            scene.plot_goal = sj.value("plot_goal", "");
            scene.emotional_goal = sj.value("emotional_goal", "");
            scene.information_goal = sj.value("information_goal", "");
            scene.external_conflict = sj.value("external_conflict", "");
            scene.internal_conflict = sj.value("internal_conflict", "");
            if (sj.contains("hidden_conflict") && !sj["hidden_conflict"].is_null()
                && sj["hidden_conflict"].is_string()) {
                scene.hidden_conflict = sj["hidden_conflict"].get<std::string>();
            }

            // Parse status
            std::string s_status_str = sj.value("status", "draft");
            if (s_status_str == "writing") scene.status = SceneStatus::Writing;
            else if (s_status_str == "completed") scene.status = SceneStatus::Completed;
            else scene.status = SceneStatus::Draft;

            if (sj.contains("style_overrides"))
                scene.style_overrides = sj["style_overrides"];

            try {
                auto created = narrative_.create_scene(world.id, std::move(scene));
                std::string old_id = sj.value("id", "");
                if (!old_id.empty()) id_map[old_id] = created.id;
            } catch (const std::exception& e) {
                spdlog::warn("import_snapshot: failed to create scene: {}", e.what());
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 9: Import diaries
    // ═══════════════════════════════════════════════════════════════
    if (payload.contains("agents") && payload["agents"].is_array()) {
        for (const auto& aj : payload["agents"]) {
            if (!aj.contains("diaries") || !aj["diaries"].is_array()) continue;
            std::string new_agent_id = remap(aj.value("id", ""));
            if (new_agent_id.empty()) continue;

            for (const auto& dj : aj["diaries"]) {
                std::string old_diary_id = dj.value("id", "");
                std::string new_diary_id = make_id("diary");

                DiaryEntry entry;
                entry.id = new_diary_id;
                entry.agent_id = new_agent_id;
                entry.scene_id = remap(dj.value("scene_id", ""));
                entry.world_time = dj.value("world_time", "");
                entry.content = dj.value("content", "");
                entry.mood = dj.value("mood", "");
                entry.status = dj.value("status", "completed");
                entry.leak_risk_level = dj.value("leak_risk_level", 0);
                entry.tokens_used = dj.value("tokens_used", 0);

                try {
                    agents_.append_diary_entry(std::move(entry));
                    if (!old_diary_id.empty()) id_map[old_diary_id] = new_diary_id;
                } catch (const std::exception& e) {
                    spdlog::warn("import_snapshot: failed to import diary: {}", e.what());
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 10: Import memory summaries
    // ═══════════════════════════════════════════════════════════════
    if (payload.contains("agents") && payload["agents"].is_array()) {
        for (const auto& aj : payload["agents"]) {
            if (!aj.contains("memory_summaries") || !aj["memory_summaries"].is_array()) continue;
            std::string new_agent_id = remap(aj.value("id", ""));
            if (new_agent_id.empty()) continue;

            for (const auto& mj : aj["memory_summaries"]) {
                std::string new_summary_id = make_id("summary");

                MemorySummary summary;
                summary.id = new_summary_id;
                summary.agent_id = new_agent_id;
                summary.period_start = mj.value("period_start", "");
                summary.period_end = mj.value("period_end", "");
                summary.summary = mj.value("summary", "");

                // Remap source diary IDs if present
                if (mj.contains("source_diary_ids") && mj["source_diary_ids"].is_array()) {
                    for (const auto& did : mj["source_diary_ids"])
                        if (did.is_string()) summary.source_diary_ids.push_back(remap(did.get<std::string>()));
                }

                try {
                    agents_.write_memory_summary(std::move(summary));
                    std::string old_summary_id = mj.value("id", "");
                    if (!old_summary_id.empty()) id_map[old_summary_id] = new_summary_id;
                } catch (const std::exception& e) {
                    spdlog::warn("import_snapshot: failed to import memory summary: {}", e.what());
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 11: Import factions (two-pass for rival_faction_ids)
    // ═══════════════════════════════════════════════════════════════
    std::vector<std::pair<std::string, std::vector<std::string>>> faction_rival_fixup;

    if (payload.contains("factions") && payload["factions"].is_array()) {
        for (const auto& fj : payload["factions"]) {
            Faction faction;
            faction.name = fj.value("name", "Unnamed Faction");
            faction.description = fj.value("description", "");
            faction.goals = fj.value("goals", "");
            faction.member_agent_ids = remap_json_arr(fj.value("member_agent_ids", nlohmann::json::array()));

            auto created = worlds_.add_faction(world.id, std::move(faction));
            std::string old_id = fj.value("id", "");
            if (!old_id.empty()) id_map[old_id] = created.id;

            // Remember rival fixup
            if (fj.contains("rival_faction_ids") && fj["rival_faction_ids"].is_array()) {
                std::vector<std::string> old_rivals;
                for (const auto& rid : fj["rival_faction_ids"])
                    if (rid.is_string()) old_rivals.push_back(rid.get<std::string>());
                if (!old_rivals.empty())
                    faction_rival_fixup.emplace_back(created.id, std::move(old_rivals));
            }
        }

        // Fixup rival_faction_ids
        for (const auto& [new_id, old_rivals] : faction_rival_fixup) {
            std::vector<std::string> new_rivals = remap_vec(old_rivals);
            // Remove empty strings from remapping failures
            new_rivals.erase(std::remove(new_rivals.begin(), new_rivals.end(), ""), new_rivals.end());
            if (!new_rivals.empty()) {
                worlds_.update_faction(world.id, new_id,
                                       {{"rival_faction_ids", new_rivals}});
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 12: Import knowledge
    // ═══════════════════════════════════════════════════════════════
    if (payload.contains("knowledge") && payload["knowledge"].is_array()) {
        for (const auto& kj : payload["knowledge"]) {
            WorldKnowledge wk;
            wk.id = make_id("wk");
            wk.category = kj.value("category", "other");
            wk.content = kj.value("content", "");
            wk.created_at = kj.value("created_at", now_iso_utc());

            if (kj.contains("tags") && kj["tags"].is_array()) {
                for (const auto& t : kj["tags"])
                    if (t.is_string()) wk.tags.push_back(t.get<std::string>());
            }
            if (kj.contains("aliases") && kj["aliases"].is_array()) {
                for (const auto& a : kj["aliases"])
                    if (a.is_string()) wk.aliases.push_back(a.get<std::string>());
            }
            wk.related_ids = remap_json_arr(kj.value("related_ids", nlohmann::json::array()));

            worlds_.add_world_knowledge(world.id, wk);
            std::string old_id = kj.value("id", "");
            if (!old_id.empty()) id_map[old_id] = wk.id;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 13: Import foreshadowing
    // ═══════════════════════════════════════════════════════════════
    // Two-pass: create first, then fixup cross-references
    struct ForeshadowFixup {
        std::string new_id;
        std::vector<std::string> old_related_fs_ids;
        std::vector<std::string> old_related_secret_ids;
    };
    std::vector<ForeshadowFixup> fs_fixups;

    if (payload.contains("foreshadowing") && payload["foreshadowing"].is_array()) {
        for (const auto& fj : payload["foreshadowing"]) {
            Foreshadowing fs;
            fs.content = fj.value("content", "");
            fs.pay_off_idea = fj.value("pay_off_idea", "");

            // Remap planted_at (chapter/scene ID)
            if (fj.contains("planted_at") && !fj["planted_at"].is_null()
                && fj["planted_at"].is_string()) {
                fs.planted_at = remap(fj["planted_at"].get<std::string>());
            }
            if (fj.contains("paid_at") && !fj["paid_at"].is_null()
                && fj["paid_at"].is_string()) {
                fs.paid_at = remap(fj["paid_at"].get<std::string>());
            }

            // Parse status
            std::string fs_status = fj.value("status", "open");
            if (fs_status == "paid") fs.status = ForeshadowStatus::Paid;
            else if (fs_status == "abandoned") fs.status = ForeshadowStatus::Abandoned;
            else fs.status = ForeshadowStatus::Open;

            // Parse hint_level
            std::string hl = fj.value("hint_level", "visible");
            if (hl == "subtle") fs.hint_level = ForeshadowHintLevel::Subtle;
            else if (hl == "obvious") fs.hint_level = ForeshadowHintLevel::Obvious;
            else fs.hint_level = ForeshadowHintLevel::Visible;

            // Parse created_by
            std::string cb = fj.value("created_by", "author");
            if (cb == "god_agent_detected") fs.created_by = ForeshadowCreatedBy::GodAgentDetected;
            else fs.created_by = ForeshadowCreatedBy::Author;

            if (fj.contains("tags") && fj["tags"].is_array()) {
                for (const auto& t : fj["tags"])
                    if (t.is_string()) fs.tags.push_back(t.get<std::string>());
            }

            auto created = foreshadowing_.plant(world.id, std::move(fs));
            std::string old_id = fj.value("id", "");
            if (!old_id.empty()) id_map[old_id] = created.id;

            // Collect cross-refs for fixup
            ForeshadowFixup ff;
            ff.new_id = created.id;
            if (fj.contains("related_foreshadowing_ids") && fj["related_foreshadowing_ids"].is_array()) {
                for (const auto& rid : fj["related_foreshadowing_ids"]) {
                    if (rid.is_string()) ff.old_related_fs_ids.push_back(rid.get<std::string>());
                }
            }
            if (fj.contains("related_secret_ids") && fj["related_secret_ids"].is_array()) {
                for (const auto& sid : fj["related_secret_ids"]) {
                    if (sid.is_string()) ff.old_related_secret_ids.push_back(sid.get<std::string>());
                }
            }
            if (!ff.old_related_fs_ids.empty() || !ff.old_related_secret_ids.empty()) {
                fs_fixups.push_back(std::move(ff));
            }
        }

        // Fixup foreshadowing cross-refs (secrets may not be imported yet, so this is best-effort)
        for (const auto& ff : fs_fixups) {
            nlohmann::json patch;
            if (!ff.old_related_fs_ids.empty()) {
                patch["related_foreshadowing_ids"] = remap_vec(ff.old_related_fs_ids);
            }
            if (!ff.old_related_secret_ids.empty()) {
                patch["related_secret_ids"] = remap_vec(ff.old_related_secret_ids);
            }
            if (!patch.empty()) {
                foreshadowing_.patch(world.id, ff.new_id, patch);
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 14: Import secrets
    // ═══════════════════════════════════════════════════════════════
    if (payload.contains("secrets") && payload["secrets"].is_array()) {
        for (const auto& sj : payload["secrets"]) {
            Secret secret;
            secret.truth = sj.value("truth", "");
            secret.public_version = sj.value("public_version", "");
            secret.stakes = sj.value("stakes", "");

            // Remap holder_id (agent)
            secret.holder_id = remap(sj.value("holder_id", ""));

            // Remap character vectors
            secret.aware_character_ids =
                remap_json_arr(sj.value("aware_character_ids", nlohmann::json::array()));
            secret.suspicious_character_ids =
                remap_json_arr(sj.value("suspicious_character_ids", nlohmann::json::array()));
            secret.related_foreshadowing_ids =
                remap_json_arr(sj.value("related_foreshadowing_ids", nlohmann::json::array()));

            if (sj.contains("planted_at") && !sj["planted_at"].is_null()
                && sj["planted_at"].is_string()) {
                secret.planted_at = remap(sj["planted_at"].get<std::string>());
            }
            if (sj.contains("exposed_at") && !sj["exposed_at"].is_null()
                && sj["exposed_at"].is_string()) {
                secret.exposed_at = remap(sj["exposed_at"].get<std::string>());
            }

            // Parse status
            std::string sec_status = sj.value("status", "active");
            if (sec_status == "exposed") secret.status = SecretStatus::Exposed;
            else if (sec_status == "abandoned") secret.status = SecretStatus::Abandoned;
            else secret.status = SecretStatus::Active;

            if (sj.contains("believed_truths"))
                secret.believed_truths = sj["believed_truths"];

            auto created = secrets_.create(world.id, std::move(secret));
            std::string old_id = sj.value("id", "");
            if (!old_id.empty()) id_map[old_id] = created.id;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 15: Import timeline events
    // ═══════════════════════════════════════════════════════════════
    if (payload.contains("timeline") && payload["timeline"].is_array()) {
        for (const auto& tj : payload["timeline"]) {
            TimelineEvent event;
            event.world_time = tj.value("world_time", "");
            event.description = tj.value("description", "");
            event.recorded_by = remap(tj.value("recorded_by", ""));
            event.affected_character_ids =
                remap_json_arr(tj.value("affected_character_ids", nlohmann::json::array()));
            event.related_scene_ids =
                remap_json_arr(tj.value("related_scene_ids", nlohmann::json::array()));

            try {
                narrative_.record_timeline_event(world.id, std::move(event));
            } catch (const std::exception& e) {
                spdlog::warn("import_snapshot: failed to record timeline event: {}", e.what());
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Step 16: Store import metadata in world config
    // ═══════════════════════════════════════════════════════════════
    {
        nlohmann::json meta;
        meta["imported_from"] = old_world_id;
        meta["imported_at"] = now_iso_utc();
        meta["schema_version"] = snapshot.value("schema_version", "unknown");
        meta["snapshot_id"] = snapshot.value("snapshot_id", "unknown");
        worlds_.update_world_config(world.id, meta);
    }

    spdlog::info("import_snapshot: imported {} entities into world {}",
                 id_map.size(), world.id);

    return result;
}

} // namespace merak::worldbuilding
