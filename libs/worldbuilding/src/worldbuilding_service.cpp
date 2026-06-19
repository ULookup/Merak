#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/pg_helpers.hpp>

#include <spdlog/spdlog.h>

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

} // namespace merak::worldbuilding
