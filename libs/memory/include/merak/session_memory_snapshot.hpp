#pragma once
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <cstdint>

namespace merak {

using UUID = std::string;
using TurnIndex = int;

struct ActiveGoal {
  std::string description;
  float progress = 0.0f;
  std::optional<std::string> blocked_by;
};

struct WorldFactChange {
  std::string entity;
  std::string field;
  std::optional<std::string> old_value;
  std::string new_value;
};

struct CharacterStateChange {
  std::string character_name;
  std::string field;   // "mood", "goal", "knowledge", "relationship"
  std::optional<std::string> old_value;
  std::string new_value;
};

struct ForeshadowingUpdate {
  std::string plant_id;
  std::string status;  // "planted", "advanced", "resolved", "abandoned"
  std::string note;
};

struct SessionMemorySnapshot {
  std::string schema_version = "1.0";
  std::string session_id;
  TurnIndex updated_turn = 0;
  std::chrono::system_clock::time_point extracted_at;

  // Narrative layer
  std::string session_title;
  std::string last_narrative_beat;

  // Goals layer
  std::vector<ActiveGoal> active_goals;
  std::vector<std::string> completed_this_turn;

  // World layer
  std::vector<WorldFactChange> world_changes;
  std::vector<CharacterStateChange> character_changes;
  std::vector<ForeshadowingUpdate> foreshadowing_updates;

  // Metacognitive layer
  std::vector<std::string> corrections;
  std::vector<std::string> learnings;
  std::vector<std::string> pending_todos;

  // Fallback
  std::string worklog;
};

} // namespace merak
