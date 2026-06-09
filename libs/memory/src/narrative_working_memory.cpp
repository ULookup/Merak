#include <merak/narrative_working_memory.hpp>
#include <sstream>
#include <algorithm>

namespace merak {

void NarrativeWorkingMemory::set_last_beat(const std::string& beat) { last_beat_ = beat; }
std::string NarrativeWorkingMemory::last_beat() const { return last_beat_; }

void NarrativeWorkingMemory::add_open_beat(const std::string& beat) { open_beats_.push_back(beat); }

void NarrativeWorkingMemory::resolve_open_beat(const std::string& beat) {
  open_beats_.erase(std::remove(open_beats_.begin(), open_beats_.end(), beat), open_beats_.end());
}

const std::vector<std::string>& NarrativeWorkingMemory::open_beats() const { return open_beats_; }

void NarrativeWorkingMemory::update_character(const AgentId& id, const CharacterMoment& moment) {
  character_states_[id] = moment;
}

std::optional<CharacterMoment> NarrativeWorkingMemory::character_state(const AgentId& id) const {
  auto it = character_states_.find(id);
  if (it != character_states_.end()) return it->second;
  return std::nullopt;
}

const std::map<AgentId, CharacterMoment>& NarrativeWorkingMemory::all_characters() const {
  return character_states_;
}

void NarrativeWorkingMemory::set_active_reminders(const std::vector<ForeshadowingRef>& reminders) {
  active_reminders_ = reminders;
}

const std::vector<ForeshadowingRef>& NarrativeWorkingMemory::active_reminders() const {
  return active_reminders_;
}

void NarrativeWorkingMemory::set_tone(const ToneConstraint& t) { tone_ = t; }
void NarrativeWorkingMemory::clear_tone() { tone_ = std::nullopt; }
const std::optional<ToneConstraint>& NarrativeWorkingMemory::tone() const { return tone_; }

void NarrativeWorkingMemory::inject_nudge(const std::string& nudge) {
  pending_nudges_.push_back(nudge);
}

std::vector<std::string> NarrativeWorkingMemory::drain_nudges() {
  auto nudges = std::move(pending_nudges_);
  pending_nudges_.clear();
  return nudges;
}

void NarrativeWorkingMemory::reset_narrative() {
  last_beat_.clear();
  open_beats_.clear();
  character_states_.clear();
  active_reminders_.clear();
  tone_ = std::nullopt;
  pending_nudges_.clear();
}

std::string NarrativeWorkingMemory::to_context_text() const {
  std::ostringstream oss;
  if (!last_beat_.empty()) {
    oss << "Last narrative beat: " << last_beat_ << "\n";
  }
  if (!open_beats_.empty()) {
    oss << "Open narrative beats:\n";
    for (auto& b : open_beats_) oss << "  - " << b << "\n";
  }
  if (!character_states_.empty()) {
    oss << "Character states:\n";
    for (auto& [id, cm] : character_states_) {
      oss << "  " << cm.name << ": mood=" << cm.mood << ", goal=" << cm.goal << "\n";
    }
  }
  if (tone_) {
    oss << "Tone: " << tone_->tone << ", pacing: " << tone_->pacing << "\n";
  }
  for (auto& n : pending_nudges_) {
    oss << "Note: " << n << "\n";
  }
  return oss.str();
}

} // namespace merak
