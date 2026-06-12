#pragma once
#include <string>
#include <cstdint>

namespace merak {

enum class SectionKind {
  Identity,
  Constraints,
  WorldContext,
  Skills,
  ToolSchemas,
  WorkingMemory,
  Memory,
  Conversation,
};

inline const char* section_kind_name(SectionKind k) {
  switch (k) {
    case SectionKind::Identity: return "Identity";
    case SectionKind::Constraints: return "Constraints";
    case SectionKind::WorldContext: return "WorldContext";
    case SectionKind::Skills: return "Skills";
    case SectionKind::ToolSchemas: return "ToolSchemas";
    case SectionKind::WorkingMemory: return "WorkingMemory";
    case SectionKind::Memory: return "Memory";
    case SectionKind::Conversation: return "Conversation";
  }
  return "Unknown";
}

enum class CompactionTier : uint8_t {
  Normal = 0,
  TrimSchemas = 1,
  CompactHistory = 2,
  AggressivePrune = 3,
};

inline const char* compaction_tier_name(CompactionTier t) {
  switch (t) {
    case CompactionTier::Normal: return "Normal";
    case CompactionTier::TrimSchemas: return "TrimSchemas";
    case CompactionTier::CompactHistory: return "CompactHistory";
    case CompactionTier::AggressivePrune: return "AggressivePrune";
  }
  return "Unknown";
}

inline bool operator>=(CompactionTier a, CompactionTier b) {
  return static_cast<uint8_t>(a) >= static_cast<uint8_t>(b);
}

enum class CacheScope : uint8_t {
  Global,    // cacheable across sessions
  Session,   // cacheable within session
  Turn,      // changes every turn, no caching
};

inline const char* cache_scope_name(CacheScope s) {
  switch (s) {
    case CacheScope::Global: return "Global";
    case CacheScope::Session: return "Session";
    case CacheScope::Turn: return "Turn";
  }
  return "Unknown";
}

enum class StallLevel : uint8_t {
  None = 0,
  SigStall = 1,    // 3 consecutive identical rounds
  ForceStop = 2,   // 5 consecutive identical rounds
};

enum class Severity : uint8_t {
  Healthy = 0,
  Info = 1,
  Warning = 2,
  Critical = 3,
};

} // namespace merak
