#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>

namespace merak {

struct AssemblyTrace {
  int turn;
  CompactionTier tier;

  struct SectionEntry {
    SectionKind kind;
    CacheScope scope;
    int budget_allocated;
    int actual_tokens;
    bool was_spilled;
    std::string content_preview;
    std::string source;
  };
  std::vector<SectionEntry> sections;

  struct ActionEntry {
    std::string description;
    int tokens_saved;
  };
  std::vector<ActionEntry> actions;

  int total_tokens_before = 0;
  int total_tokens_after = 0;
  int tokens_saved = 0;
  double cache_hit_ratio = 0.0;

  nlohmann::json to_json() const {
    nlohmann::json j;
    j["turn"] = turn;
    j["tier"] = compaction_tier_name(tier);
    j["tokens_before"] = total_tokens_before;
    j["tokens_after"] = total_tokens_after;
    j["tokens_saved"] = tokens_saved;
    j["cache_hit_ratio"] = cache_hit_ratio;

    j["sections"] = nlohmann::json::array();
    for (auto& s : sections) {
      nlohmann::json sj;
      sj["kind"] = section_kind_name(s.kind);
      sj["scope"] = cache_scope_name(s.scope);
      sj["budget"] = s.budget_allocated;
      sj["actual"] = s.actual_tokens;
      sj["spilled"] = s.was_spilled;
      sj["preview"] = s.content_preview;
      sj["source"] = s.source;
      j["sections"].push_back(sj);
    }

    j["actions"] = nlohmann::json::array();
    for (auto& a : actions) {
      nlohmann::json aj;
      aj["description"] = a.description;
      aj["tokens_saved"] = a.tokens_saved;
      j["actions"].push_back(aj);
    }

    return j;
  }
};

} // namespace merak
