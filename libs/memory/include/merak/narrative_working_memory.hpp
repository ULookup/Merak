#pragma once
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace merak {

using AgentId = std::string;

struct CharacterMoment {
  std::string name;
  std::string mood;
  std::string goal;
  std::vector<std::string> secrets_exposed;
};

struct ForeshadowingRef {
  std::string plant_id;
  std::string description;
  float importance = 0.0f;
};

struct ToneConstraint {
  std::string tone;
  std::string pacing;
};

class NarrativeWorkingMemory {
public:
  void set_last_beat(const std::string& beat);
  std::string last_beat() const;

  void add_open_beat(const std::string& beat);
  void resolve_open_beat(const std::string& beat);
  const std::vector<std::string>& open_beats() const;

  void update_character(const AgentId& id, const CharacterMoment& moment);
  std::optional<CharacterMoment> character_state(const AgentId& id) const;
  const std::map<AgentId, CharacterMoment>& all_characters() const;

  void set_active_reminders(const std::vector<ForeshadowingRef>& reminders);
  const std::vector<ForeshadowingRef>& active_reminders() const;

  void set_tone(const ToneConstraint& tone);
  void clear_tone();
  const std::optional<ToneConstraint>& tone() const;

  void inject_nudge(const std::string& nudge);
  std::vector<std::string> drain_nudges();

  void reset_narrative();

  std::string to_context_text() const;

private:
  std::string last_beat_;
  std::vector<std::string> open_beats_;
  std::map<AgentId, CharacterMoment> character_states_;
  std::vector<ForeshadowingRef> active_reminders_;
  std::optional<ToneConstraint> tone_;
  std::vector<std::string> pending_nudges_;
};

} // namespace merak
