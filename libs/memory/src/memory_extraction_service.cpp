#include <merak/memory_extraction_service.hpp>
#include <spdlog/spdlog.h>

namespace merak {

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

  last_extraction_turn_ = turn;
  pending_extractions_++;

  std::thread([this, session_id, turn, messages_summary]() {
    try {
      SessionMemorySnapshot snap;
      snap.session_id = session_id;
      snap.updated_turn = turn;
      snap.extracted_at = std::chrono::system_clock::now();
      snap.last_narrative_beat = messages_summary.substr(0, 200);
      snap.worklog = messages_summary;

      {
        std::lock_guard lock(mutex_);
        latest_snapshots_[session_id] = snap;
      }

      if (journal_) {
        journal_->append("memory_extracted", {
          {"session_id", session_id},
          {"turn", turn},
          {"summary_length", messages_summary.size()}
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
