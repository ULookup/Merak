#include <merak/memory_extraction_service.hpp>
#include <merak/llm_provider.hpp>
#include <merak/execution.hpp>
#include <spdlog/spdlog.h>
#include <future>

namespace merak {

static const char* EXTRACTION_SYSTEM_PROMPT = R"PROMPT(
You are a memory extraction engine for a novel-writing AI agent. Your task is to extract structured narrative memory from conversation summaries.

Output a JSON object following this schema exactly:
{
  "session_title": "brief title for this session (max 80 chars)",
  "last_narrative_beat": "one sentence describing the most recent story event",
  "active_goals": [
    {"description": "goal text", "progress": 0.5, "blocked_by": null}
  ],
  "world_changes": [
    {"entity": "entity name", "field": "field", "old_value": null, "new_value": "new value"}
  ],
  "character_changes": [
    {"character_name": "name", "field": "mood|goal|knowledge|relationship", "old_value": null, "new_value": "new value"}
  ],
  "foreshadowing_updates": [
    {"plant_id": "id", "status": "planted|advanced|resolved|abandoned", "note": "detail"}
  ],
  "corrections": ["correction text"],
  "learnings": ["learning text"],
  "pending_todos": ["todo text"]
}

Rules:
- Only include fields that have data. Empty arrays should be [].
- progress must be a float between 0.0 and 1.0.
- blocked_by should be null or a string.
- old_value should be null if this is a new fact (not an update).
- Extract at most 3 items per array to keep output compact.
- Use the original language of the content.
)PROMPT";

MemoryExtractionService::MemoryExtractionService(Config cfg,
                                                   std::shared_ptr<LlmProvider> llm,
                                                   std::shared_ptr<SessionJournal> journal)
  : cfg_(std::move(cfg)), llm_(std::move(llm)), journal_(std::move(journal)) {}

bool MemoryExtractionService::should_extract(TurnIndex turn, bool had_creation,
                                               bool had_secret, bool had_foreshadowing,
                                               bool is_scene_boundary, bool had_correction) {
  if (turn - last_extraction_turn_ < cfg_.min_turns_between_extractions) return false;
  if (pending_extractions_ >= cfg_.max_queued_extractions) return false;
  if (had_creation || had_secret || had_foreshadowing || is_scene_boundary || had_correction)
    return true;
  if (turn - last_extraction_turn_ >= 5) return true;
  return false;
}

void MemoryExtractionService::extract_async(
    const std::string& session_id, TurnIndex turn,
    const std::string& messages_summary,
    bool had_creation, bool had_secret_exposed,
    bool had_foreshadowing_resolved, bool is_scene_boundary,
    bool had_user_correction) {

  if (!should_extract(turn, had_creation, had_secret_exposed,
                       had_foreshadowing_resolved, is_scene_boundary,
                       had_user_correction)) {
    return;
  }

  if (!llm_) {
    spdlog::warn("MemoryExtraction: no LLM provider, skipping extraction");
    return;
  }

  last_extraction_turn_ = turn;
  pending_extractions_++;

  // Capture a CancellationToken for timeout control
  auto cancellation = std::make_shared<CancellationToken>();

  std::thread([this, session_id, turn, messages_summary, cancellation]() {
    try {
      // Build extraction prompt
      ChatRequest req;
      req.model = cfg_.model;
      req.max_output_tokens = cfg_.max_output_tokens;
      req.enable_cache = false;
      req.enable_thinking = false; // no thinking needed for structured extraction

      Message sys_msg;
      sys_msg.role = "system";
      sys_msg.content = EXTRACTION_SYSTEM_PROMPT;
      req.messages.push_back(sys_msg);

      Message user_msg;
      user_msg.role = "user";
      user_msg.content = "Recent events in this session:\n\n" + messages_summary;
      req.messages.push_back(user_msg);

      // Fire LLM call with timeout
      auto future = llm_->chat(req,
          [](StreamChunk) {}, // no streaming needed
          cancellation);

      auto status = future.wait_for(cfg_.timeout);
      if (status == std::future_status::timeout) {
        cancellation->cancel();
        spdlog::warn("MemoryExtraction: LLM call timed out after {}ms",
                     cfg_.timeout.count());
        pending_extractions_--;
        return;
      }

      auto response = future.get();
      std::string raw_text = response.text;

      // Parse JSON response
      SessionMemorySnapshot snap;
      snap.session_id = session_id;
      snap.updated_turn = turn;
      snap.extracted_at = std::chrono::system_clock::now();

      try {
        auto j = nlohmann::json::parse(raw_text);

        if (j.contains("session_title") && j["session_title"].is_string()) {
          snap.session_title = j["session_title"].get<std::string>();
        }
        if (j.contains("last_narrative_beat") && j["last_narrative_beat"].is_string()) {
          snap.last_narrative_beat = j["last_narrative_beat"].get<std::string>();
        }

        // Active goals
        if (j.contains("active_goals") && j["active_goals"].is_array()) {
          for (auto& g : j["active_goals"]) {
            ActiveGoal goal;
            if (g.contains("description") && g["description"].is_string()) {
              goal.description = g["description"].get<std::string>();
            }
            if (g.contains("progress") && g["progress"].is_number()) {
              goal.progress = g["progress"].get<float>();
            }
            if (g.contains("blocked_by") && g["blocked_by"].is_string()) {
              goal.blocked_by = g["blocked_by"].get<std::string>();
            }
            if (!goal.description.empty()) {
              snap.active_goals.push_back(std::move(goal));
            }
          }
        }

        // World changes
        if (j.contains("world_changes") && j["world_changes"].is_array()) {
          for (auto& wc : j["world_changes"]) {
            WorldFactChange change;
            if (wc.contains("entity")) change.entity = wc["entity"].get<std::string>();
            if (wc.contains("field")) change.field = wc["field"].get<std::string>();
            if (wc.contains("old_value") && wc["old_value"].is_string()) {
              change.old_value = wc["old_value"].get<std::string>();
            }
            if (wc.contains("new_value")) change.new_value = wc["new_value"].get<std::string>();
            if (!change.entity.empty()) {
              snap.world_changes.push_back(std::move(change));
            }
          }
        }

        // Character changes
        if (j.contains("character_changes") && j["character_changes"].is_array()) {
          for (auto& cc : j["character_changes"]) {
            CharacterStateChange change;
            if (cc.contains("character_name")) change.character_name = cc["character_name"].get<std::string>();
            if (cc.contains("field")) change.field = cc["field"].get<std::string>();
            if (cc.contains("old_value") && cc["old_value"].is_string()) {
              change.old_value = cc["old_value"].get<std::string>();
            }
            if (cc.contains("new_value")) change.new_value = cc["new_value"].get<std::string>();
            if (!change.character_name.empty()) {
              snap.character_changes.push_back(std::move(change));
            }
          }
        }

        // Foreshadowing updates
        if (j.contains("foreshadowing_updates") && j["foreshadowing_updates"].is_array()) {
          for (auto& fu : j["foreshadowing_updates"]) {
            ForeshadowingUpdate update;
            if (fu.contains("plant_id")) update.plant_id = fu["plant_id"].get<std::string>();
            if (fu.contains("status")) update.status = fu["status"].get<std::string>();
            if (fu.contains("note")) update.note = fu["note"].get<std::string>();
            if (!update.plant_id.empty()) {
              snap.foreshadowing_updates.push_back(std::move(update));
            }
          }
        }

        // Metacognitive
        if (j.contains("corrections") && j["corrections"].is_array()) {
          for (auto& c : j["corrections"]) {
            if (c.is_string()) snap.corrections.push_back(c.get<std::string>());
          }
        }
        if (j.contains("learnings") && j["learnings"].is_array()) {
          for (auto& l : j["learnings"]) {
            if (l.is_string()) snap.learnings.push_back(l.get<std::string>());
          }
        }
        if (j.contains("pending_todos") && j["pending_todos"].is_array()) {
          for (auto& t : j["pending_todos"]) {
            if (t.is_string()) snap.pending_todos.push_back(t.get<std::string>());
          }
        }

        // Fallback: always keep a worklog copy
        snap.worklog = messages_summary;

        spdlog::info("MemoryExtraction: extracted {} goals, {} world changes, {} char changes, {} foreshadowing updates",
                     snap.active_goals.size(), snap.world_changes.size(),
                     snap.character_changes.size(), snap.foreshadowing_updates.size());

      } catch (const nlohmann::json::parse_error& e) {
        spdlog::warn("MemoryExtraction: JSON parse failed: {} — falling back to worklog", e.what());
        snap.last_narrative_beat = messages_summary.substr(0, 200);
        snap.worklog = messages_summary;
      }

      // Store snapshot
      {
        std::lock_guard lock(mutex_);
        latest_snapshots_[session_id] = snap;
      }

      // Write journal entry
      if (journal_) {
        journal_->append("memory_extracted", {
          {"session_id", session_id},
          {"turn", turn},
          {"summary_length", messages_summary.size()},
          {"goals_extracted", snap.active_goals.size()},
          {"world_changes", snap.world_changes.size()},
          {"character_changes", snap.character_changes.size()},
          {"foreshadowing_updates", snap.foreshadowing_updates.size()}
        });
      }
    } catch (const std::exception& e) {
      spdlog::warn("Memory extraction failed: {}", e.what());
    }
    pending_extractions_--;
  }).detach();
}

std::optional<SessionMemorySnapshot> MemoryExtractionService::latest_snapshot(
    const std::string& session_id) const {
  std::lock_guard lock(mutex_);
  auto it = latest_snapshots_.find(session_id);
  if (it != latest_snapshots_.end()) return it->second;
  return std::nullopt;
}

} // namespace merak
