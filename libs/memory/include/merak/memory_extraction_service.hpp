#pragma once
#include <merak/session_memory_snapshot.hpp>
#include <merak/session_journal.hpp>
#include <merak/llm_provider.hpp>
#include <memory>
#include <future>
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace merak {

class MemoryExtractionService {
public:
  struct Config {
    std::string model = "gpt-4o-mini";
    int max_output_tokens = 4096;
    std::chrono::milliseconds timeout{30000};
    int min_turns_between_extractions = 2;
    int max_queued_extractions = 3;
  };

  MemoryExtractionService(Config cfg, std::shared_ptr<LlmProvider> llm,
                          std::shared_ptr<SessionJournal> journal);

  void extract_async(const std::string& session_id, TurnIndex turn,
                     const std::string& messages_summary,
                     bool had_creation, bool had_secret_exposed,
                     bool had_foreshadowing_resolved, bool is_scene_boundary,
                     bool had_user_correction);

  std::optional<SessionMemorySnapshot> latest_snapshot(const std::string& session_id) const;

private:
  bool should_extract(TurnIndex turn, bool had_creation, bool had_secret_exposed,
                      bool had_foreshadowing_resolved, bool is_scene_boundary,
                      bool had_user_correction);

  Config cfg_;
  std::shared_ptr<LlmProvider> llm_;
  std::shared_ptr<SessionJournal> journal_;

  TurnIndex last_extraction_turn_ = -1;
  int pending_extractions_ = 0;
  std::map<std::string, SessionMemorySnapshot> latest_snapshots_;
  mutable std::mutex mutex_;
};

} // namespace merak
