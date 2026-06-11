#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <merak/worldbuilding/ids.hpp>

#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string>

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
      orchestrator_(worlds_, agents_, narrative_, foreshadowing_, secrets_, voice_, kg_provider_.get()),
      kg_provider_(std::move(kg_provider)) {}

void WorldbuildingService::initialize() {
    worlds_.initialize();
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
    return orchestrator_.finish_scene(world_id, scene_id, final_markdown);
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
    return voice_.check_all(voice_.list_fingerprints());
}

// ── Preview builders (no DB write) ──────────────────────────────────────────

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

// ── Store and retrieve pending creations ───────────────────────────────────

std::string WorldbuildingService::store_pending_creation(
        const std::string& world_id, const std::string& tool_name,
        const nlohmann::json& params, const nlohmann::json& preview) {
    std::lock_guard lock(pending_mutex_);
    PendingCreation pc;
    pc.creation_id = "creation_" + make_id("c");
    pc.tool_name = tool_name;
    pc.world_id = world_id;
    pc.params = params;
    pc.preview = preview;
    pc.created_at = std::chrono::steady_clock::now();
    pending_creations_[pc.creation_id] = pc;
    return pc.creation_id;
}

std::optional<PendingCreation> WorldbuildingService::get_pending_creation(
        const std::string& creation_id) const {
    std::lock_guard lock(pending_mutex_);
    auto it = pending_creations_.find(creation_id);
    if (it == pending_creations_.end()) return std::nullopt;
    return it->second;
}

// ── Resolution (writes to DB on allow/modify) ──────────────────────────────

nlohmann::json WorldbuildingService::resolve_creation(
        const std::string& creation_id,
        const std::string& decision,
        const nlohmann::json& modifications) {

    // Step 1: Under lock, find and copy, erase for non-deny
    PendingCreation pc;
    {
        std::lock_guard lock(pending_mutex_);
        auto it = pending_creations_.find(creation_id);
        if (it == pending_creations_.end()) {
            throw std::runtime_error("Pending creation not found: " + creation_id);
        }
        pc = std::move(it->second);

        if (decision == "deny") {
            pending_creations_.erase(it);
            return {{"ok", true}, {"decision", "deny"}, {"creation_id", creation_id}};
        }

        // For allow/modify: erase from map BEFORE DB write
        pending_creations_.erase(it);
    }
    // Lock released here — DB work happens without holding mutex

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
        // Step 4: On failure, re-insert pc into map for retry
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

} // namespace merak::worldbuilding
