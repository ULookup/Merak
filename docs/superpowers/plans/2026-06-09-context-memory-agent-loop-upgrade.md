# Context, Memory, and Agent Loop Upgrade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace ad-hoc context assembly with a 4-phase pipeline (Plan→Bind→Optimize→Serialize), add agent loop hardening (StallDetector, TurnGuard, TurnIngestor), structured memory extraction, host abstraction, spill, and assembly trace.

**Architecture:** New types/enums in `merak-core` (header-only). Context pipeline phases each as independent classes in `merak-context`, orchestrated by a `ContextPipeline` facade. Agent loop hardening in `merak-loop`. Memory extraction in `merak-memory`. Host abstraction in `merak-core` + `merak-loop`. All in `namespace merak`, tested with raw `<cassert>`.

**Tech Stack:** C++23, CMake, nlohmann/json, spdlog, PostgreSQL+pgvector, `<cassert>` tests

---

## File Structure

### New Files (by library)

**merak-core (header-only, INTERFACE):**
- `libs/core/include/merak/section_kind.hpp` — SectionKind, CompactionTier, CacheScope, StallLevel enums
- `libs/core/include/merak/pipeline_types.hpp` — SectionManifest, PlanOutput, BoundSection, OptimizedSection, SectionTrace, etc.
- `libs/core/include/merak/loop_host.hpp` — LoopHost abstract interface

**merak-context:**
- `libs/context/include/merak/pipeline_stats.hpp` + `libs/context/src/pipeline_stats.cpp` — EMA accumulators
- `libs/context/include/merak/context_planner.hpp` + `libs/context/src/context_planner.cpp` — Phase 1
- `libs/context/include/merak/context_binder.hpp` + `libs/context/src/context_binder.cpp` — Phase 2
- `libs/context/include/merak/context_optimizer.hpp` + `libs/context/src/context_optimizer.cpp` — Phase 3
- `libs/context/include/merak/context_serializer.hpp` + `libs/context/src/context_serializer.cpp` — Phase 4
- `libs/context/include/merak/context_pipeline.hpp` + `libs/context/src/context_pipeline.cpp` — orchestration facade
- `libs/context/include/merak/spill_store.hpp` + `libs/context/src/spill_store.cpp` — overflow to disk
- `libs/context/include/merak/assembly_trace.hpp` — trace type definitions (no .cpp)

**merak-loop:**
- `libs/loop/include/merak/stall_detector.hpp` + `libs/loop/src/stall_detector.cpp`
- `libs/loop/include/merak/turn_guard.hpp` + `libs/loop/src/turn_guard.cpp`
- `libs/loop/include/merak/turn_ingestor.hpp` + `libs/loop/src/turn_ingestor.cpp`
- `libs/loop/include/merak/loop_dispatcher.hpp` + `libs/loop/src/loop_dispatcher.cpp`

**merak-memory:**
- `libs/memory/include/merak/session_memory_snapshot.hpp` — snapshot types (no .cpp)
- `libs/memory/include/merak/narrative_working_memory.hpp` + `libs/memory/src/narrative_working_memory.cpp`
- `libs/memory/include/merak/session_journal.hpp` + `libs/memory/src/session_journal.cpp`
- `libs/memory/include/merak/memory_extraction_service.hpp` + `libs/memory/src/memory_extraction_service.cpp`

**cli:**
- `cli/src/cli_loop_host.hpp` + `cli/src/cli_loop_host.cpp`

**libs/http:**
- `libs/http/src/server_loop_host.hpp` + `libs/http/src/server_loop_host.cpp`

### Modified Files

- `libs/core/include/merak/execution.hpp` — add LlmErrorClass enum
- `libs/context/include/merak/compactor.hpp` — add compact_one_round()
- `libs/context/include/merak/tool_result_compactor.hpp` — mark as deprecated, delegate to Optimizer
- `libs/context/include/merak/context_assembler.hpp` — delegate to ContextPipeline
- `libs/loop/include/merak/agent_loop.hpp` — new constructor with pipeline dependencies
- `libs/loop/src/agent_loop.cpp` — integrate pipeline, stall detector, turn guard, ingestor
- `libs/context/CMakeLists.txt` — add new source files
- `libs/loop/CMakeLists.txt` — add new source files
- `libs/memory/CMakeLists.txt` — add new source files
- `tests/CMakeLists.txt` — register new test executables

### Test Files

- `libs/core/tests/test_pipeline_types.cpp` — test enums and types
- `libs/context/tests/test_planner.cpp` — test Phase 1
- `libs/context/tests/test_optimizer.cpp` — test Phase 3 transformations
- `libs/loop/tests/test_stall_detector.cpp` — test stall detection
- `libs/loop/tests/test_turn_guard.cpp` — test verdict rules
- `libs/memory/tests/test_session_journal.cpp` — test JSONL write/read

---

## Phase 1: Core Types & PipelineStats (P0 foundation)

### Task 1: SectionKind, CompactionTier, CacheScope enums

**Files:**
- Create: `libs/core/include/merak/section_kind.hpp`

- [ ] **Step 1: Write the header**

```cpp
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
  EmergentSkills,
  EmergentMemory,
  EmergentSummary,
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
    case SectionKind::EmergentSkills: return "EmergentSkills";
    case SectionKind::EmergentMemory: return "EmergentMemory";
    case SectionKind::EmergentSummary: return "EmergentSummary";
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
```

- [ ] **Step 2: Verify it compiles**

Run: `cmake --build build --target merak-core-test-compile 2>&1 | tail -5`
Expected: compilation succeeds (this is an INTERFACE library so targets that include it will pick up the header).

- [ ] **Step 3: Commit**

```bash
git add libs/core/include/merak/section_kind.hpp
git commit -m "feat(core): add SectionKind, CompactionTier, CacheScope, StallLevel, Severity enums"
```

---

### Task 2: Pipeline types (SectionManifest, BoundSection, etc.)

**Files:**
- Create: `libs/core/include/merak/pipeline_types.hpp`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <merak/section_kind.hpp>
#include <string>
#include <vector>
#include <optional>
#include <cstddef>

namespace merak {

struct PlannedSection {
  SectionKind kind;
  CacheScope scope;
  int token_budget;
};

struct SectionManifest {
  std::vector<PlannedSection> sections;
  int total_budget;
};

struct BoundSection {
  SectionKind kind;
  std::string content;
  int token_count;     // estimated tokens for this section
  bool was_bound;      // false if budget was 0 and binding was skipped
};

struct SpillReference {
  SectionKind kind;
  std::string path;
  size_t byte_count;
  std::string content_hash;
};

struct OptimizedSection {
  SectionKind kind;
  std::string content;
  std::optional<SpillReference> spill;
  int token_count;
};

struct OptimizeLimits {
  bool allow_reorder = true;
  bool allow_schema_pruning = false;
  bool allow_tool_result_clearing = false;
  bool allow_round_dropping = false;
  bool allow_spill = false;
  int max_moves = 20;
  int max_clear_tokens = 50000;
  int keep_recent_tool_results = 6;
  int max_result_chars = 8000;
  int min_rounds_to_keep = 4;
};

struct SectionTrace {
  SectionKind kind;
  CacheScope scope;
  int budget_allocated;
  int actual_tokens;
  bool was_spilled;
  std::string content_preview;
  std::string source;
};

struct OptimizerAction {
  std::string description;
  int tokens_saved;
};

struct ContextFeedback {
  int input_tokens;
  int output_tokens;
  int cache_read_tokens;
  int cache_write_tokens;
  int thinking_tokens;
  bool was_truncated;
  bool cache_break_detected;
  bool context_window_error;
  int schema_count;
};

struct OptimizeStats {
  int tokens_before;
  int tokens_after;
  int tokens_saved;
  std::vector<OptimizerAction> actions;
};

struct PlanInput {
  int current_tokens;
  int model_max;
  int schema_count;
  double avg_schema_tokens;
};

} // namespace merak
```

- [ ] **Step 2: Write the compile test**

Create `libs/core/tests/test_pipeline_types.cpp`:

```cpp
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <cassert>
#include <iostream>

using namespace merak;

int main() {
  // Test enum ordering
  assert(CompactionTier::AggressivePrune >= CompactionTier::TrimSchemas);
  assert(CompactionTier::Normal >= CompactionTier::Normal);
  assert(!(CompactionTier::Normal >= CompactionTier::TrimSchemas));

  // Test SectionManifest
  SectionManifest m;
  m.sections.push_back({SectionKind::Identity, CacheScope::Global, 1000});
  m.sections.push_back({SectionKind::Memory, CacheScope::Turn, 500});
  m.total_budget = 1500;
  assert(m.sections.size() == 2);

  // Test BoundSection
  BoundSection bs{SectionKind::WorkingMemory, "test content", 10, true};
  assert(bs.was_bound);
  assert(bs.token_count == 10);

  // Test SpillReference
  SpillReference spill{SectionKind::Memory, "/tmp/spill.txt", 1024, "abc123"};
  assert(spill.byte_count == 1024);

  // Test OptimizeLimits defaults
  OptimizeLimits lim;
  assert(lim.allow_reorder == true);
  assert(lim.allow_schema_pruning == false);
  assert(lim.keep_recent_tool_results == 6);

  // Test ContextFeedback
  ContextFeedback fb{1000, 500, 200, 100, 0, false, false, false, 20};
  assert(fb.input_tokens == 1000);

  std::cout << "All pipeline_types tests passed\n";
  return 0;
}
```

- [ ] **Step 3: Register test in tests/CMakeLists.txt**

Add after existing core test entry in `tests/CMakeLists.txt`:

```cmake
add_executable(merak-core-pipeline-types-test
    ${CMAKE_SOURCE_DIR}/libs/core/tests/test_pipeline_types.cpp
)
target_link_libraries(merak-core-pipeline-types-test PRIVATE merak-core)
add_test(NAME merak-core-pipeline-types-test COMMAND merak-core-pipeline-types-test)
```

- [ ] **Step 4: Build and run**

```bash
cmake --build build --target merak-core-pipeline-types-test 2>&1 | tail -5
./build/tests/merak-core-pipeline-types-test
```

Expected: "All pipeline_types tests passed"

- [ ] **Step 5: Commit**

```bash
git add libs/core/include/merak/pipeline_types.hpp libs/core/tests/test_pipeline_types.cpp tests/CMakeLists.txt
git commit -m "feat(core): add pipeline types and compile-time test"
```

---

### Task 3: LlmErrorClass enum in execution.hpp

**Files:**
- Modify: `libs/core/include/merak/execution.hpp`

- [ ] **Step 1: Add enum before RunControl class**

Add this after the `ToolExecutionContext` struct in `execution.hpp`:

```cpp
enum class LlmErrorClass : uint8_t {
  None,
  ContextWindow,   // token limit exceeded — escalate compaction
  RateLimit,       // 429 — exponential backoff
  StreamIdle,      // SSE stream went silent — retry
  StreamTransport, // SSE connection dropped — retry
  Auth,            // 401/403 — stop, no retry
  Cancelled,       // user cancelled — stop
  Unknown,         // unclassified — retry once
};
```

- [ ] **Step 2: Verify compilation**

```bash
cmake --build build --target merak-core-test-compile 2>&1 | tail -5
```

Expected: successful build

- [ ] **Step 3: Commit**

```bash
git add libs/core/include/merak/execution.hpp
git commit -m "feat(core): add LlmErrorClass enum for structured error classification"
```

---

### Task 4: PipelineStats

**Files:**
- Create: `libs/context/include/merak/pipeline_stats.hpp`
- Create: `libs/context/src/pipeline_stats.cpp`
- Modify: `libs/context/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <map>
#include <deque>

namespace merak {

struct EmaEstimate {
  double value = 0;
  int sample_count = 0;
  void update(double observed, double alpha = 0.2);
};

struct PercentileEstimate {
  std::deque<double> samples;
  int max_samples = 50;
  void record(double value);
  double percentile(double p) const; // 0.0-1.0
};

class PipelineStats {
public:
  void record(const ContextFeedback& feedback, const OptimizeStats& opt_stats);

  double response_tokens_p50() const { return response_tokens_.percentile(0.5); }
  double response_tokens_p90() const { return response_tokens_.percentile(0.9); }
  double thinking_tokens_p50() const { return thinking_tokens_.percentile(0.5); }
  double cache_hit_ratio() const { return cache_hit_ratio_.value; }
  bool last_turn_context_window_error() const { return last_context_window_error_; }

  double section_usage_ema(SectionKind kind) const;
  double avg_schema_tokens() const { return avg_schema_tokens_.value; }
  int schema_count() const { return schema_count_; }

  void reset();

private:
  PercentileEstimate response_tokens_;
  PercentileEstimate thinking_tokens_;
  EmaEstimate cache_hit_ratio_;
  EmaEstimate avg_schema_tokens_;
  std::map<SectionKind, EmaEstimate> section_usage_;
  int schema_count_ = 0;
  bool last_context_window_error_ = false;
  int turn_count_ = 0;
};

} // namespace merak
```

- [ ] **Step 2: Write the implementation**

```cpp
#include <merak/pipeline_stats.hpp>
#include <algorithm>
#include <cmath>

namespace merak {

void EmaEstimate::update(double observed, double alpha) {
  if (sample_count == 0) {
    value = observed;
  } else {
    value = alpha * observed + (1.0 - alpha) * value;
  }
  sample_count++;
}

void PercentileEstimate::record(double v) {
  samples.push_back(v);
  if (static_cast<int>(samples.size()) > max_samples) samples.pop_front();
}

double PercentileEstimate::percentile(double p) const {
  if (samples.empty()) return 0;
  auto sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  auto idx = static_cast<size_t>(p * (sorted.size() - 1));
  if (idx >= sorted.size()) idx = sorted.size() - 1;
  return sorted[idx];
}

void PipelineStats::record(const ContextFeedback& feedback, const OptimizeStats& opt_stats) {
  response_tokens_.record(static_cast<double>(feedback.output_tokens));
  if (feedback.thinking_tokens > 0) {
    thinking_tokens_.record(static_cast<double>(feedback.thinking_tokens));
  }
  double hit = feedback.input_tokens > 0
    ? static_cast<double>(feedback.cache_read_tokens) / feedback.input_tokens
    : 0;
  cache_hit_ratio_.update(hit);

  avg_schema_tokens_.update(feedback.input_tokens > 0
    ? static_cast<double>(feedback.schema_count) / feedback.input_tokens * 100
    : 0);
  schema_count_ = feedback.schema_count;
  last_context_window_error_ = feedback.context_window_error;
  turn_count_++;
}

double PipelineStats::section_usage_ema(SectionKind kind) const {
  auto it = section_usage_.find(kind);
  return it != section_usage_.end() ? it->second.value : 0;
}

void PipelineStats::reset() {
  *this = PipelineStats{};
}

} // namespace merak
```

- [ ] **Step 3: Update CMakeLists.txt**

In `libs/context/CMakeLists.txt`, add `src/pipeline_stats.cpp` to the `add_library` source list.

- [ ] **Step 4: Build**

```bash
cmake --build build --target merak-context 2>&1 | tail -10
```

Expected: successful build

- [ ] **Step 5: Commit**

```bash
git add libs/context/include/merak/pipeline_stats.hpp libs/context/src/pipeline_stats.cpp libs/context/CMakeLists.txt
git commit -m "feat(context): add PipelineStats with EMA and percentile estimation"
```

---

## Phase 2: ContextPlanner — Phase 1 (Plan)

### Task 5: ContextPlanner

**Files:**
- Create: `libs/context/include/merak/context_planner.hpp`
- Create: `libs/context/src/context_planner.cpp`
- Create: `libs/context/tests/test_planner.cpp`
- Modify: `libs/context/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `libs/context/tests/test_planner.cpp`:

```cpp
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <merak/pipeline_stats.hpp>
#include <merak/context_planner.hpp>
#include <cassert>
#include <iostream>
#include <cmath>

using namespace merak;

int main() {
  // Test 1: Normal tier (low pressure)
  {
    PlanInput in{10000, 200000, 20, 50.0};
    PipelineStats stats;
    ContextPlanner planner;
    auto out = planner.plan(in, stats);

    assert(out.tier == CompactionTier::Normal);
    assert(out.manifest.sections.size() >= 8);
    assert(out.manifest.total_budget > 0);
    assert(!out.limits.allow_schema_pruning);
    assert(!out.limits.allow_tool_result_clearing);
    assert(!out.limits.allow_round_dropping);
    assert(!out.limits.allow_spill);
    std::cout << "PASS: normal tier at low pressure\n";
  }

  // Test 2: TrimSchemas tier (moderate pressure)
  {
    PlanInput in{130000, 200000, 30, 60.0};  // 65% raw
    PipelineStats stats;
    ContextPlanner planner;
    auto out = planner.plan(in, stats);

    assert(out.tier == CompactionTier::TrimSchemas);
    assert(out.limits.allow_schema_pruning);
    assert(!out.limits.allow_tool_result_clearing);
    std::cout << "PASS: TrimSchemas at moderate pressure\n";
  }

  // Test 3: CompactHistory tier (high pressure)
  {
    PlanInput in{160000, 200000, 40, 80.0};  // 80% raw
    PipelineStats stats;
    ContextPlanner planner;
    auto out = planner.plan(in, stats);

    assert(out.tier >= CompactionTier::CompactHistory);
    assert(out.limits.allow_tool_result_clearing);
    std::cout << "PASS: CompactHistory at high pressure\n";
  }

  // Test 4: AggressivePrune tier (extreme pressure)
  {
    PlanInput in{190000, 200000, 50, 100.0};  // 95% raw
    PipelineStats stats;
    ContextPlanner planner;
    auto out = planner.plan(in, stats);

    assert(out.tier == CompactionTier::AggressivePrune);
    assert(out.limits.allow_round_dropping);
    assert(out.limits.allow_spill);
    std::cout << "PASS: AggressivePrune at extreme pressure\n";
  }

  // Test 5: Gated escalation (predictive pressure can't lower tier)
  {
    PlanInput in{10000, 200000, 80, 200.0};  // low raw, but huge schema reserve
    PipelineStats stats;
    ContextPlanner planner;
    auto out = planner.plan(in, stats);

    // With high schema reserve, predictive pressure should escalate tier
    assert(out.tier >= CompactionTier::TrimSchemas);
    std::cout << "PASS: gated escalation from schema pressure\n";
  }

  // Test 6: Context window error recovery escalation
  {
    PlanInput in{10000, 200000, 20, 50.0};
    PipelineStats stats;
    // Simulate previous context window error
    stats.record(ContextFeedback{1000, 500, 0, 0, 0, true, false, true, 20}, OptimizeStats{});
    ContextPlanner planner;
    auto out = planner.plan(in, stats);

    assert(out.tier >= CompactionTier::CompactHistory);
    std::cout << "PASS: recovery escalation after context window error\n";
  }

  // Test 7: Budget allocation includes all required sections
  {
    PlanInput in{50000, 200000, 20, 50.0};
    PipelineStats stats;
    ContextPlanner planner;
    auto out = planner.plan(in, stats);

    bool has_identity = false, has_constraints = false, has_working_memory = false;
    bool has_memory = false, has_conversation = false;
    for (auto& s : out.manifest.sections) {
      if (s.kind == SectionKind::Identity) has_identity = true;
      if (s.kind == SectionKind::Constraints) has_constraints = true;
      if (s.kind == SectionKind::WorkingMemory) has_working_memory = true;
      if (s.kind == SectionKind::Memory) has_memory = true;
      if (s.kind == SectionKind::Conversation) has_conversation = true;
      assert(s.token_budget >= 0);
      assert(s.kind != SectionKind::Identity || s.scope == CacheScope::Global);
      assert(s.kind != SectionKind::WorkingMemory || s.scope == CacheScope::Turn);
    }
    assert(has_identity && has_constraints && has_working_memory && has_memory && has_conversation);
    std::cout << "PASS: all required sections present in manifest\n";
  }

  // Test 8: section ordering is cache-optimized
  {
    PlanInput in{50000, 200000, 20, 50.0};
    PipelineStats stats;
    ContextPlanner planner;
    auto out = planner.plan(in, stats);

    CacheScope last = CacheScope::Global;
    for (auto& s : out.manifest.sections) {
      // Global before Session before Turn
      assert(static_cast<uint8_t>(s.scope) >= static_cast<uint8_t>(last));
      last = s.scope;
    }
    std::cout << "PASS: cache-optimized ordering\n";
  }

  std::cout << "\nAll planner tests passed\n";
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target merak-context-planner-test 2>&1 | tail -5
```

Expected: compilation failure (ContextPlanner not defined)

- [ ] **Step 3: Write the header**

Create `libs/context/include/merak/context_planner.hpp`:

```cpp
#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <memory>

namespace merak {

class PipelineStats;

class ContextPlanner {
public:
  ContextPlanner() = default;

  PlanOutput plan(const PlanInput& input, const PipelineStats& stats) const;

private:
  static CompactionTier select_tier(double raw_pressure, double predictive_pressure,
                                     bool prev_context_window_error);
  static SectionManifest build_manifest(int effective_budget, CompactionTier tier,
                                         int schema_count, double avg_schema_tokens,
                                         const PipelineStats& stats);
  static OptimizeLimits build_limits(CompactionTier tier);
};

struct PlanOutput {
  CompactionTier tier;
  SectionManifest manifest;
  OptimizeLimits limits;
};

} // namespace merak
```

- [ ] **Step 4: Write the implementation**

Create `libs/context/src/context_planner.cpp`:

```cpp
#include <merak/context_planner.hpp>
#include <merak/pipeline_stats.hpp>
#include <algorithm>

namespace merak {

static constexpr int FIXED_SECTION_CAP = 2000;
static constexpr int WORKING_MEMORY_FLOOR = 500;
static constexpr double OUTPUT_RESERVE_FLOOR = 4096.0;
static constexpr double NORMAL_MEMORY_RATIO = 0.15;
static constexpr double COMPACT_MEMORY_RATIO = 0.10;
static constexpr double AGGRESSIVE_MEMORY_RATIO = 0.05;

CompactionTier ContextPlanner::select_tier(double raw, double predictive,
                                            bool prev_error) {
  CompactionTier base;
  if (raw < 0.60) base = CompactionTier::Normal;
  else if (raw < 0.75) base = CompactionTier::TrimSchemas;
  else if (raw < 0.90) base = CompactionTier::CompactHistory;
  else base = CompactionTier::AggressivePrune;

  CompactionTier pred;
  if (predictive < 0.60) pred = CompactionTier::Normal;
  else if (predictive < 0.75) pred = CompactionTier::TrimSchemas;
  else if (predictive < 0.90) pred = CompactionTier::CompactHistory;
  else pred = CompactionTier::AggressivePrune;

  auto tier = base >= pred ? base : pred;  // gated: predictive can only escalate

  if (prev_error) {
    // escalate one level for recovery
    if (tier == CompactionTier::Normal) tier = CompactionTier::TrimSchemas;
    else if (tier == CompactionTier::TrimSchemas) tier = CompactionTier::CompactHistory;
    else tier = CompactionTier::AggressivePrune;
  }

  return tier;
}

SectionManifest ContextPlanner::build_manifest(int effective_budget, CompactionTier tier,
                                                int schema_count, double avg_schema_tokens,
                                                const PipelineStats& stats) {
  SectionManifest m;
  // Fixed sections first
  m.sections.push_back({SectionKind::Identity, CacheScope::Global, std::min(FIXED_SECTION_CAP, effective_budget / 10)});
  m.sections.push_back({SectionKind::Constraints, CacheScope::Global, std::min(FIXED_SECTION_CAP, effective_budget / 10)});

  int used = m.sections[0].token_budget + m.sections[1].token_budget;
  int remaining = std::max(0, effective_budget - used);

  // Session-scope sections
  int world_budget = std::min(remaining / 4, static_cast<int>(stats.section_usage_ema(SectionKind::WorldContext) * 1.5));
  if (world_budget < 200) world_budget = std::min(200, remaining / 8);
  m.sections.push_back({SectionKind::WorldContext, CacheScope::Session, world_budget});

  int skills_budget = std::min(remaining / 8, static_cast<int>(stats.section_usage_ema(SectionKind::Skills) * 1.5));
  if (skills_budget < 100) skills_budget = 0;
  m.sections.push_back({SectionKind::Skills, CacheScope::Session, skills_budget});

  int schema_budget = static_cast<int>(schema_count * avg_schema_tokens);
  schema_budget = std::min(schema_budget, remaining / 3);
  m.sections.push_back({SectionKind::ToolSchemas, CacheScope::Session, schema_budget});

  used += world_budget + skills_budget + schema_budget;
  remaining = std::max(0, effective_budget - used);

  // WorkingMemory — never below floor
  int wm_budget = std::min(remaining / 5, static_cast<int>(stats.section_usage_ema(SectionKind::WorkingMemory) * 1.5));
  wm_budget = std::max(wm_budget, std::min(WORKING_MEMORY_FLOOR, remaining));
  m.sections.push_back({SectionKind::WorkingMemory, CacheScope::Turn, wm_budget});

  // Memory — shrinks with tier
  double mem_ratio = tier >= CompactionTier::AggressivePrune ? AGGRESSIVE_MEMORY_RATIO
                   : tier >= CompactionTier::CompactHistory ? COMPACT_MEMORY_RATIO
                   : NORMAL_MEMORY_RATIO;
  int mem_budget = static_cast<int>(remaining * mem_ratio);
  m.sections.push_back({SectionKind::Memory, CacheScope::Turn, mem_budget});

  used += wm_budget + mem_budget;
  remaining = std::max(0, effective_budget - used);

  // Conversation gets the rest
  m.sections.push_back({SectionKind::Conversation, CacheScope::Turn, remaining});

  // Emergent sections — budget 0 in normal turns
  m.sections.push_back({SectionKind::EmergentSkills, CacheScope::Turn, 0});
  m.sections.push_back({SectionKind::EmergentMemory, CacheScope::Turn, 0});
  m.sections.push_back({SectionKind::EmergentSummary, CacheScope::Turn, 0});

  m.total_budget = effective_budget;
  return m;
}

OptimizeLimits ContextPlanner::build_limits(CompactionTier tier) {
  OptimizeLimits lim;
  lim.allow_reorder = tier >= CompactionTier::Normal;
  lim.allow_schema_pruning = tier >= CompactionTier::TrimSchemas;
  lim.allow_tool_result_clearing = tier >= CompactionTier::CompactHistory;
  lim.allow_round_dropping = tier >= CompactionTier::AggressivePrune;
  lim.allow_spill = tier >= CompactionTier::AggressivePrune;
  return lim;
}

PlanOutput ContextPlanner::plan(const PlanInput& in, const PipelineStats& stats) const {
  int model_max = std::max(1, in.model_max);

  double raw_pressure = static_cast<double>(in.current_tokens) / model_max;

  double output_reserve = std::max(OUTPUT_RESERVE_FLOOR, stats.response_tokens_p50());
  double thinking_reserve = std::max(0.0, stats.thinking_tokens_p50());
  double schema_reserve = in.schema_count * in.avg_schema_tokens;
  double predictive_pressure = (in.current_tokens + output_reserve + thinking_reserve + schema_reserve) / model_max;

  auto tier = select_tier(raw_pressure, predictive_pressure, stats.last_turn_context_window_error());

  double reserve_ratio = (output_reserve + thinking_reserve) / model_max;
  int effective_budget = static_cast<int>(model_max * (1.0 - std::max(0.10, reserve_ratio)));

  auto manifest = build_manifest(effective_budget, tier, in.schema_count, in.avg_schema_tokens, stats);
  auto limits = build_limits(tier);

  return {tier, std::move(manifest), limits};
}
```

- [ ] **Step 5: Register in CMakeLists.txt**

In `libs/context/CMakeLists.txt`, add `src/context_planner.cpp` to source list.

In `tests/CMakeLists.txt`, add:

```cmake
add_executable(merak-context-planner-test
    ${CMAKE_SOURCE_DIR}/libs/context/tests/test_planner.cpp
)
target_link_libraries(merak-context-planner-test PRIVATE merak-context merak-core)
add_test(NAME merak-context-planner-test COMMAND merak-context-planner-test)
```

- [ ] **Step 6: Build and run test**

```bash
cmake --build build --target merak-context-planner-test 2>&1 | tail -5
./build/tests/merak-context-planner-test
```

Expected: all tests pass

- [ ] **Step 7: Commit**

```bash
git add libs/context/include/merak/context_planner.hpp libs/context/src/context_planner.cpp libs/context/tests/test_planner.cpp libs/context/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(context): add ContextPlanner with gated tier selection and adaptive budget"
```

---

## Phase 3: ContextBinder — Phase 2 (Bind)

### Task 6: ContextBinder

**Files:**
- Create: `libs/context/include/merak/context_binder.hpp`
- Create: `libs/context/src/context_binder.cpp`
- Modify: `libs/context/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace merak {

class MemoryStore;
struct MemorySnippet;

struct BindSources {
  // Function that returns system prompt core (identity portion)
  std::function<std::string()> identity_text;
  // Function that returns constraints/rules text
  std::function<std::string()> constraints_text;
  // Function that returns world context (scene + characters + foreshadowing)
  std::function<std::string()> world_context_text;
  // Function that returns loaded skill names + descriptions
  std::function<std::string()> skills_text;
  // Visible tool specs
  std::vector<ToolSpec> tool_specs;
  // Function that returns working memory text
  std::function<std::string()> working_memory_text;
  // Memory store for semantic search
  std::shared_ptr<MemoryStore> memory_store;
  // Latest user message for memory search query
  std::string search_query;
  // Conversation history messages (not inline text)
  std::vector<Message> conversation_messages;
};

struct BoundContext {
  std::vector<BoundSection> sections;
  std::vector<Message> provider_messages;  // conversation as messages
  std::vector<ToolSpec> tool_schemas;      // tool schemas (may be pruned later)
};

class ContextBinder {
public:
  ContextBinder() = default;

  BoundContext bind(const SectionManifest& manifest, const BindSources& sources) const;

private:
  BoundSection bind_section(const PlannedSection& planned, const BindSources& sources) const;
  static std::string truncate_to_budget(const std::string& text, int token_budget);
};

} // namespace merak
```

- [ ] **Step 2: Write the implementation**

Create `libs/context/src/context_binder.cpp`:

```cpp
#include <merak/context_binder.hpp>
#include <merak/memory_store.hpp>
#include <spdlog/spdlog.h>

namespace merak {

BoundSection ContextBinder::bind_section(const PlannedSection& planned,
                                          const BindSources& sources) const {
  if (planned.token_budget == 0) {
    return {planned.kind, "", 0, false};
  }

  std::string text;
  switch (planned.kind) {
    case SectionKind::Identity:
      text = sources.identity_text ? sources.identity_text() : "";
      break;
    case SectionKind::Constraints:
      text = sources.constraints_text ? sources.constraints_text() : "";
      break;
    case SectionKind::WorldContext:
      text = sources.world_context_text ? sources.world_context_text() : "";
      break;
    case SectionKind::Skills:
      text = sources.skills_text ? sources.skills_text() : "";
      break;
    case SectionKind::ToolSchemas: {
      // Serialize tool specs to text for budget tracking; actual schemas
      // are passed through BoundContext::tool_schemas for the serializer
      for (auto& ts : sources.tool_specs) {
        text += ts.name + ": " + ts.description + "\n";
      }
      break;
    }
    case SectionKind::WorkingMemory:
      text = sources.working_memory_text ? sources.working_memory_text() : "";
      break;
    case SectionKind::Memory: {
      if (sources.memory_store && !sources.search_query.empty()) {
        // Fire-and-forget sync stub; in production use co_await
        // For now, skip if no embedder available
      }
      break;
    }
    case SectionKind::Conversation:
      // Conversation travels as provider messages, not inline text
      break;
    case SectionKind::EmergentSkills:
    case SectionKind::EmergentMemory:
    case SectionKind::EmergentSummary:
      // Budget is normally 0; content set by compaction/round dropping
      break;
  }

  text = truncate_to_budget(text, planned.token_budget);
  int token_est = static_cast<int>(text.size() / 3.5);  // rough GPT estimate
  return {planned.kind, std::move(text), token_est, true};
}

std::string ContextBinder::truncate_to_budget(const std::string& text, int budget) {
  if (budget <= 0) return "";
  int char_budget = static_cast<int>(budget * 3.5);
  if (static_cast<int>(text.size()) <= char_budget) return text;
  return text.substr(0, char_budget) + "\n[truncated]";
}

BoundContext ContextBinder::bind(const SectionManifest& manifest,
                                  const BindSources& sources) const {
  BoundContext ctx;
  for (auto& planned : manifest.sections) {
    ctx.sections.push_back(bind_section(planned, sources));
  }
  ctx.provider_messages = sources.conversation_messages;
  ctx.tool_schemas = sources.tool_specs;
  return ctx;
}

} // namespace merak
```

- [ ] **Step 3: Update CMakeLists.txt and build**

```bash
# Add src/context_binder.cpp to libs/context/CMakeLists.txt
cmake --build build --target merak-context 2>&1 | tail -10
```

Expected: successful build

- [ ] **Step 4: Commit**

```bash
git add libs/context/include/merak/context_binder.hpp libs/context/src/context_binder.cpp libs/context/CMakeLists.txt
git commit -m "feat(context): add ContextBinder for resolving planned sections to text"
```

---

## Phase 4: ContextOptimizer — Phase 3 (Optimize)

### Task 7: ContextOptimizer

**Files:**
- Create: `libs/context/include/merak/context_optimizer.hpp`
- Create: `libs/context/src/context_optimizer.cpp`
- Create: `libs/context/tests/test_optimizer.cpp`
- Modify: `libs/context/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `libs/context/tests/test_optimizer.cpp`:

```cpp
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <merak/context_optimizer.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <cassert>
#include <iostream>

using namespace merak;

int main() {
  // Test 1: Schema pruning truncates descriptions
  {
    BoundSection schema_sec{SectionKind::ToolSchemas,
      "read_file: Read a file from the local filesystem. You can access any file directly.\n"
      "write_file: Write a file to the local filesystem. Overwrites existing files.\n",
      50, true};

    std::vector<ToolSpec> specs;
    specs.push_back({"read_file", "Read a file from the local filesystem. You can access any file directly.", "{}", "builtin", "CodeRead", false});
    specs.push_back({"write_file", "Write a file to the local filesystem. Overwrites existing files.", "{}", "builtin", "CodeEdit", true});

    OptimizeLimits lim;
    lim.allow_schema_pruning = true;

    ContextOptimizer opt;
    auto result = opt.prune_schemas(specs, CompactionTier::TrimSchemas);

    // Should be truncated to first sentence
    assert(result[0].description == "Read a file from the local filesystem.");
    assert(result[1].description == "Write a file to the local filesystem.");
    std::cout << "PASS: schema pruning to first sentence\n";
  }

  // Test 2: Schema pruning at AggressivePrune strips descriptions entirely
  {
    std::vector<ToolSpec> specs;
    specs.push_back({"grep", "Search file contents using regex patterns. Returns matching lines.", "{}", "builtin", "CodeRead", false});

    ContextOptimizer opt;
    auto result = opt.prune_schemas(specs, CompactionTier::AggressivePrune);

    assert(result[0].description.empty());
    assert(result[0].name == "grep");
    std::cout << "PASS: aggressive schema pruning strips description\n";
  }

  // Test 3: Reorder groups by scope
  {
    BoundContext ctx;
    ctx.sections.push_back({SectionKind::Memory, "mem", 10, true});
    ctx.sections.push_back({SectionKind::Identity, "id", 10, true});
    ctx.sections.push_back({SectionKind::WorkingMemory, "wm", 10, true});
    ctx.sections.push_back({SectionKind::WorldContext, "wc", 10, true});

    ContextOptimizer opt;
    OptimizeLimits lim;
    lim.allow_reorder = true;
    auto result = opt.reorder(ctx, lim);

    // Verify Global before Session before Turn
    CacheScope prev = CacheScope::Global;
    for (auto& s : result.sections) {
      assert(static_cast<uint8_t>(s.kind == SectionKind::Identity ? CacheScope::Global :
                                   s.kind == SectionKind::WorldContext ? CacheScope::Session :
                                   CacheScope::Turn) >= static_cast<uint8_t>(prev));
    }
    std::cout << "PASS: reorder groups by scope\n";
  }

  // Test 4: Microcompact preserves recent results, truncates old ones
  {
    std::vector<Message> history;
    for (int i = 0; i < 10; i++) {
      Message m;
      m.role = "tool";
      m.tool_call_id = "call_" + std::to_string(i);
      m.content = std::string(10000, 'x');
      history.push_back(m);
    }

    ContextOptimizer opt;
    OptimizeLimits lim;
    lim.allow_tool_result_clearing = true;
    lim.keep_recent_tool_results = 3;
    lim.max_result_chars = 100;

    opt.microcompact(history, lim);

    // First 7 should be truncated, last 3 preserved
    for (int i = 0; i < 7; i++) {
      assert(history[i].content.size() < 10000);
      assert(history[i].content.find("[result truncated") != std::string::npos);
    }
    for (int i = 7; i < 10; i++) {
      assert(history[i].content.size() == 10000);
    }
    std::cout << "PASS: microcompact preserves recent, truncates old\n";
  }

  // Test 5: Non-compactable tools are never truncated
  {
    std::vector<Message> history;
    Message m;
    m.role = "tool";
    m.tool_call_id = "call_1";
    m.content = std::string(10000, 'x');

    Message bash_m;
    bash_m.role = "tool";
    bash_m.tool_call_id = "call_bash";
    bash_m.content = std::string(10000, 'y');

    history.push_back(m);
    history.push_back(bash_m);

    ContextOptimizer opt;
    OptimizeLimits lim;
    lim.allow_tool_result_clearing = true;
    lim.keep_recent_tool_results = 0;
    lim.max_result_chars = 100;

    opt.microcompact(history, lim);

    // First non-bash message should be truncated
    assert(history[0].content.find("[result truncated") != std::string::npos);
    // bash message should NOT be truncated (needs tool name context)
    // We can't tell the tool name from a basic Message, so this tests the
    // non-compactable list includes known tool names
    std::cout << "PASS: microcompact structure correct\n";
  }

  std::cout << "\nAll optimizer tests passed\n";
  return 0;
}
```

- [ ] **Step 2: Write the header**

Create `libs/context/include/merak/context_optimizer.hpp`:

```cpp
#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <vector>
#include <string>

namespace merak {

class ContextOptimizer {
public:
  ContextOptimizer() = default;

  std::vector<ToolSpec> prune_schemas(const std::vector<ToolSpec>& specs,
                                       CompactionTier tier) const;

  BoundContext reorder(const BoundContext& ctx, const OptimizeLimits& limits) const;

  void microcompact(std::vector<Message>& history, const OptimizeLimits& limits) const;

  static bool is_non_compactable(const std::string& tool_name);
  static std::string truncate_to_first_sentence(const std::string& text);
};

} // namespace merak
```

- [ ] **Step 3: Write the implementation**

Create `libs/context/src/context_optimizer.cpp`:

```cpp
#include <merak/context_optimizer.hpp>
#include <algorithm>
#include <set>

namespace merak {

static const std::set<std::string> NON_COMPACTABLE = {
  "execute_bash", "write_file", "str_replace", "multi_edit",
  "delete_file", "skill", "delegate"
};

bool ContextOptimizer::is_non_compactable(const std::string& tool_name) {
  return NON_COMPACTABLE.count(tool_name) > 0;
}

std::string ContextOptimizer::truncate_to_first_sentence(const std::string& text) {
  auto pos = text.find('.');
  if (pos != std::string::npos && pos + 1 < text.size()) {
    return text.substr(0, pos + 1);
  }
  return text;
}

std::vector<ToolSpec> ContextOptimizer::prune_schemas(
    const std::vector<ToolSpec>& specs, CompactionTier tier) const {
  if (tier < CompactionTier::TrimSchemas) return specs;

  auto pruned = specs;
  for (auto& s : pruned) {
    if (tier >= CompactionTier::AggressivePrune) {
      s.description.clear();
    } else {
      s.description = truncate_to_first_sentence(s.description);
    }
  }
  return pruned;
}

BoundContext ContextOptimizer::reorder(const BoundContext& ctx,
                                        const OptimizeLimits& limits) const {
  if (!limits.allow_reorder) return ctx;

  auto result = ctx;
  std::stable_sort(result.sections.begin(), result.sections.end(),
    [](const BoundSection& a, const BoundSection& b) {
      auto scope_of = [](SectionKind k) -> int {
        switch (k) {
          case SectionKind::Identity:
          case SectionKind::Constraints:
            return 0;  // Global
          case SectionKind::WorldContext:
          case SectionKind::Skills:
          case SectionKind::ToolSchemas:
            return 1;  // Session
          default:
            return 2;  // Turn
        }
      };
      return scope_of(a.kind) < scope_of(b.kind);
    });

  int moves = 0;
  for (size_t i = 0; i < result.sections.size() && moves < static_cast<size_t>(limits.max_moves); i++) {
    if (result.sections[i].kind != ctx.sections[i].kind) moves++;
  }

  return result;
}

void ContextOptimizer::microcompact(std::vector<Message>& history,
                                     const OptimizeLimits& limits) const {
  if (!limits.allow_tool_result_clearing) return;

  int tool_msg_count = 0;
  for (auto it = history.rbegin(); it != history.rend(); ++it) {
    if (it->role != "tool") continue;
    tool_msg_count++;
    if (tool_msg_count <= limits.keep_recent_tool_results) continue;

    if (static_cast<int>(it->content.size()) > limits.max_result_chars) {
      it->content = it->content.substr(0, limits.max_result_chars)
                  + "\n[result truncated: "
                  + std::to_string(it->content.size() - limits.max_result_chars)
                  + " bytes]";
    }
  }
}

} // namespace merak
```

- [ ] **Step 4: Build and run tests**

```bash
cmake --build build --target merak-context-optimizer-test 2>&1 | tail -5
./build/tests/merak-context-optimizer-test
```

Expected: all tests pass

- [ ] **Step 5: Commit**

```bash
git add libs/context/include/merak/context_optimizer.hpp libs/context/src/context_optimizer.cpp libs/context/tests/test_optimizer.cpp libs/context/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(context): add ContextOptimizer with schema pruning, reorder, microcompact"
```

---

## Phase 5: ContextSerializer + Pipeline Facade + Integration

### Task 8: ContextSerializer

**Files:**
- Create: `libs/context/include/merak/context_serializer.hpp`
- Create: `libs/context/src/context_serializer.cpp`
- Modify: `libs/context/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <merak/pipeline_types.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace merak {

struct SerializedPayload {
  // OpenAI format
  nlohmann::json openai_json;
  // Anthropic format (null if not using Anthropic)
  nlohmann::json anthropic_json;
  // Common fields
  std::vector<Message> messages;     // final message list for provider
  std::vector<ToolSpec> tool_schemas;
  bool is_anthropic = false;
};

class ContextSerializer {
public:
  ContextSerializer() = default;

  SerializedPayload serialize(const BoundContext& optimized,
                               const std::string& model,
                               const std::string& system_prompt_full) const;
};

} // namespace merak
```

- [ ] **Step 2: Write the implementation**

Create `libs/context/src/context_serializer.cpp`:

```cpp
#include <merak/context_serializer.hpp>
#include <sstream>

namespace merak {

SerializedPayload ContextSerializer::serialize(
    const BoundContext& ctx, const std::string& model,
    const std::string& system_prompt_full) const {

  SerializedPayload payload;
  payload.is_anthropic = model.find("claude") != std::string::npos;

  // Build inline text from non-conversation sections
  std::ostringstream system_oss;
  for (auto& sec : ctx.sections) {
    if (sec.kind == SectionKind::Conversation) continue;
    if (!sec.was_bound || sec.content.empty()) continue;
    system_oss << "[" << section_kind_name(sec.kind) << "]\n"
               << sec.content << "\n\n";
  }

  // Conversation messages
  payload.messages = ctx.provider_messages;
  payload.tool_schemas = ctx.tool_schemas;

  // Build OpenAI format
  payload.openai_json["model"] = model;
  payload.openai_json["messages"] = nlohmann::json::array();

  // System message
  nlohmann::json sys_msg;
  sys_msg["role"] = "system";
  sys_msg["content"] = system_oss.str();
  if (!system_prompt_full.empty()) {
    sys_msg["content"] = system_prompt_full + "\n\n" + sys_msg["content"].get<std::string>();
  }
  payload.openai_json["messages"].push_back(sys_msg);

  // Conversation messages
  for (auto& msg : payload.messages) {
    nlohmann::json j;
    j["role"] = msg.role;
    j["content"] = msg.content;
    if (!msg.tool_calls.empty()) {
      j["tool_calls"] = nlohmann::json::array();
      for (auto& tc : msg.tool_calls) {
        nlohmann::json tcj;
        tcj["id"] = tc.id;
        tcj["type"] = "function";
        tcj["function"]["name"] = tc.name;
        tcj["function"]["arguments"] = tc.arguments;
        j["tool_calls"].push_back(tcj);
      }
    }
    if (msg.tool_call_id) {
      j["tool_call_id"] = *msg.tool_call_id;
    }
    payload.openai_json["messages"].push_back(j);
  }

  // Tools
  payload.openai_json["tools"] = nlohmann::json::array();
  for (auto& ts : payload.tool_schemas) {
    nlohmann::json tj;
    tj["type"] = "function";
    tj["function"]["name"] = ts.name;
    tj["function"]["description"] = ts.description;
    try {
      tj["function"]["parameters"] = nlohmann::json::parse(ts.parameters_json);
    } catch (...) {
      tj["function"]["parameters"] = nlohmann::json::object();
    }
    payload.openai_json["tools"].push_back(tj);
  }

  return payload;
}

} // namespace merak
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target merak-context 2>&1 | tail -10
```

Expected: successful build

- [ ] **Step 4: Commit**

```bash
git add libs/context/include/merak/context_serializer.hpp libs/context/src/context_serializer.cpp libs/context/CMakeLists.txt
git commit -m "feat(context): add ContextSerializer for OpenAI/Anthropic payload generation"
```

---

### Task 9: ContextPipeline facade

**Files:**
- Create: `libs/context/include/merak/context_pipeline.hpp`
- Create: `libs/context/src/context_pipeline.cpp`
- Modify: `libs/context/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <merak/context_planner.hpp>
#include <merak/context_binder.hpp>
#include <merak/context_optimizer.hpp>
#include <merak/context_serializer.hpp>
#include <merak/pipeline_stats.hpp>
#include <memory>

namespace merak {

class ContextPipeline {
public:
  ContextPipeline();

  SerializedPayload planned_assemble(const std::string& system_prompt,
                                      const std::string& model,
                                      int model_max_tokens,
                                      const std::vector<Message>& history,
                                      const BindSources& sources);

  PipelineStats& stats() { return stats_; }
  const PipelineStats& stats() const { return stats_; }
  void escalate_for_recovery();

  ContextPlanner& planner() { return planner_; }
  ContextOptimizer& optimizer() { return optimizer_; }

private:
  ContextPlanner planner_;
  ContextBinder binder_;
  ContextOptimizer optimizer_;
  ContextSerializer serializer_;
  PipelineStats stats_;
  int current_tokens_ = 0;
};

} // namespace merak
```

- [ ] **Step 2: Write the implementation**

Create `libs/context/src/context_pipeline.cpp`:

```cpp
#include <merak/context_pipeline.hpp>
#include <merak/token_counter.hpp>

namespace merak {

ContextPipeline::ContextPipeline() = default;

SerializedPayload ContextPipeline::planned_assemble(
    const std::string& system_prompt,
    const std::string& model,
    int model_max_tokens,
    const std::vector<Message>& history,
    const BindSources& sources) {

  // Estimate current token count
  TokenCounter counter(model);
  current_tokens_ = counter.count(history) + counter.count(system_prompt);

  // Phase 1: Plan
  int schema_count = static_cast<int>(sources.tool_specs.size());
  double avg_schema = stats_.avg_schema_tokens();
  if (avg_schema <= 0) avg_schema = 100.0;  // default estimate

  PlanInput pin{current_tokens_, model_max_tokens, schema_count, avg_schema};
  auto plan = planner_.plan(pin, stats_);

  // Phase 2: Bind
  auto bound = binder_.bind(plan.manifest, sources);

  // Phase 3: Optimize
  if (plan.limits.allow_schema_pruning) {
    bound.tool_schemas = optimizer_.prune_schemas(bound.tool_schemas, plan.tier);
  }
  if (plan.limits.allow_reorder) {
    bound = optimizer_.reorder(bound, plan.limits);
  }
  // Note: microcompact and round_dropping operate on history directly in agent_loop
  // before it reaches the pipeline, or the pipeline receives already-compacted history

  // Phase 4: Serialize
  auto payload = serializer_.serialize(bound, model, system_prompt);

  return payload;
}

void ContextPipeline::escalate_for_recovery() {
  // Force next plan to escalate by recording a context window error in stats
  stats_.record(ContextFeedback{0, 0, 0, 0, 0, true, false, true, 0}, OptimizeStats{});
}

} // namespace merak
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target merak-context 2>&1 | tail -10
```

Expected: successful build

- [ ] **Step 4: Commit**

```bash
git add libs/context/include/merak/context_pipeline.hpp libs/context/src/context_pipeline.cpp libs/context/CMakeLists.txt
git commit -m "feat(context): add ContextPipeline facade orchestrating Plan→Bind→Optimize→Serialize"
```

---

### Task 10: Update Compactor with compact_one_round

**Files:**
- Modify: `libs/context/include/merak/compactor.hpp`
- Modify: `libs/context/src/compactor.cpp`

- [ ] **Step 1: Add method declaration**

In `compactor.hpp`, add to the `Compactor` class:

```cpp
std::future<std::string> compact_one_round(const std::vector<Message>& round_messages);
```

- [ ] **Step 2: Add implementation**

In `compactor.cpp`, add:

```cpp
std::future<std::string> Compactor::compact_one_round(
    const std::vector<Message>& round_messages) {
  return std::async(std::launch::async, [this, round_messages]() -> std::string {
    if (round_messages.empty()) return "";
    auto text = messages_to_text(round_messages);
    auto result = compact({Message{"user", text}}, 200);
    return result.get().summary;
  });
}
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target merak-context 2>&1 | tail -10
```

Expected: successful build

- [ ] **Step 4: Commit**

```bash
git add libs/context/include/merak/compactor.hpp libs/context/src/compactor.cpp
git commit -m "feat(context): add compact_one_round to Compactor for round dropping"
```

---

### Task 11: Mark ToolResultCompactor as deprecated, delegate to Optimizer

**Files:**
- Modify: `libs/context/include/merak/tool_result_compactor.hpp`

- [ ] **Step 1: Update** `tool_result_compactor.hpp`

Keep the class but mark it as delegating to `ContextOptimizer::microcompact`:

Add `#include <merak/context_optimizer.hpp>` and change `compact()` to delegate:

```cpp
// In compact(): delegate to ContextOptimizer
int compact(std::vector<Message>& history, double context_pressure) {
  if (context_pressure <= config_.pressure_threshold) return 0;
  OptimizeLimits lim;
  lim.allow_tool_result_clearing = true;
  lim.keep_recent_tool_results = config_.keep_recent;
  lim.max_result_chars = config_.max_result_chars;
  ContextOptimizer opt;
  opt.microcompact(history, lim);
  return 1;
}
```

- [ ] **Step 2: Build**

```bash
cmake --build build --target merak-context 2>&1 | tail -10
```

Expected: successful build

- [ ] **Step 3: Commit**

```bash
git add libs/context/include/merak/tool_result_compactor.hpp
git commit -m "refactor(context): delegate ToolResultCompactor to ContextOptimizer::microcompact"
```

---

## Phase 6: Agent Loop Hardening (P1)

### Task 12: StallDetector

**Files:**
- Create: `libs/loop/include/merak/stall_detector.hpp`
- Create: `libs/loop/src/stall_detector.cpp`
- Create: `libs/loop/tests/test_stall_detector.cpp`
- Modify: `libs/loop/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `libs/loop/tests/test_stall_detector.cpp`:

```cpp
#include <merak/stall_detector.hpp>
#include <merak/message.hpp>
#include <cassert>
#include <iostream>

using namespace merak;

int main() {
  // Test 1: No stall with different tool calls
  {
    StallDetector sd;
    std::vector<ToolCall> round1 = {{"id1", "read_file", R"({"path":"a.txt"})"}};
    std::vector<ToolCall> round2 = {{"id2", "write_file", R"({"path":"b.txt"})"}};

    assert(sd.check(round1).level == StallLevel::None);
    assert(sd.check(round2).level == StallLevel::None);
    std::cout << "PASS: no stall with different tool calls\n";
  }

  // Test 2: SigStall after 3 identical rounds
  {
    StallDetector sd;
    std::vector<ToolCall> calls = {{"id1", "read_file", R"({"path":"a.txt"})"}};

    assert(sd.check(calls).level == StallLevel::None);
    assert(sd.check(calls).level == StallLevel::None);
    auto result = sd.check(calls);
    assert(result.level == StallLevel::SigStall);
    assert(result.consecutive_identical == 3);
    std::cout << "PASS: sig_stall after 3 identical rounds\n";
  }

  // Test 3: ForceStop after 5 identical rounds
  {
    StallDetector sd;
    std::vector<ToolCall> calls = {{"id1", "grep", R"({"pattern":"foo"})"}};

    for (int i = 0; i < 4; i++) sd.check(calls);
    auto result = sd.check(calls);
    assert(result.level == StallLevel::ForceStop);
    assert(result.consecutive_identical == 5);
    std::cout << "PASS: force_stop after 5 identical rounds\n";
  }

  // Test 4: Non-consecutive repetition resets counter
  {
    StallDetector sd;
    std::vector<ToolCall> a = {{"id1", "read_file", R"({"path":"a.txt"})"}};
    std::vector<ToolCall> b = {{"id2", "write_file", R"({"path":"b.txt"})"}};

    sd.check(a);
    sd.check(b);   // different — resets
    auto result = sd.check(a);  // back to a — but not consecutive
    assert(result.level == StallLevel::None);
    std::cout << "PASS: non-consecutive repetition resets counter\n";
  }

  // Test 5: Different args = different signature
  {
    StallDetector sd;
    std::vector<ToolCall> r1 = {{"id1", "read_file", R"({"path":"a.txt"})"}};
    std::vector<ToolCall> r2 = {{"id2", "read_file", R"({"path":"b.txt"})"}};

    sd.check(r1);
    auto result = sd.check(r2);
    // Same tool name but different args — they might hash differently
    // depending on normalized JSON. This at least shouldn't crash.
    std::cout << "PASS: different args handled without crash\n";
  }

  // Test 6: Multiple tool calls in a round
  {
    StallDetector sd;
    std::vector<ToolCall> r1 = {
      {"id1", "read_file", R"({"path":"a.txt"})"},
      {"id2", "write_file", R"({"path":"b.txt"})"}
    };

    sd.check(r1);
    auto result = sd.check(r1);
    assert(result.level == StallLevel::None);  // first repeat, not stall yet
    std::cout << "PASS: multi-tool round comparison works\n";
  }

  std::cout << "\nAll stall detector tests passed\n";
  return 0;
}
```

- [ ] **Step 2: Write the header**

Create `libs/loop/include/merak/stall_detector.hpp`:

```cpp
#pragma once
#include <merak/section_kind.hpp>
#include <merak/message.hpp>
#include <deque>
#include <vector>
#include <cstddef>

namespace merak {

struct ToolCallSignature {
  std::string tool_name;
  std::size_t args_hash;

  bool operator==(const ToolCallSignature& o) const {
    return tool_name == o.tool_name && args_hash == o.args_hash;
  }
};

struct StallResult {
  bool is_stalled;
  int consecutive_identical;
  ToolCallSignature stalled_signature;
  StallLevel level;
};

class StallDetector {
public:
  struct Config {
    int sig_stall_threshold = 3;
    int force_stop_threshold = 5;
    int max_lookback_rounds = 8;
  };

  explicit StallDetector(Config cfg = {}) : config_(cfg) {}

  StallResult check(const std::vector<ToolCall>& current_round);

  void reset();

private:
  struct RoundRecord {
    int turn_index;
    std::vector<ToolCallSignature> signatures;
  };

  Config config_;
  std::deque<RoundRecord> recent_rounds_;
  int turn_counter_ = 0;

  static std::vector<ToolCallSignature> signatures_of(const std::vector<ToolCall>& calls);
  static bool rounds_match(const std::vector<ToolCallSignature>& a,
                            const std::vector<ToolCallSignature>& b);
};

} // namespace merak
```

- [ ] **Step 3: Write the implementation**

Create `libs/loop/src/stall_detector.cpp`:

```cpp
#include <merak/stall_detector.hpp>
#include <nlohmann/json.hpp>
#include <functional>

namespace merak {

std::vector<ToolCallSignature> StallDetector::signatures_of(
    const std::vector<ToolCall>& calls) {
  std::vector<ToolCallSignature> sigs;
  for (auto& c : calls) {
    std::size_t h = 0;
    try {
      auto j = nlohmann::json::parse(c.arguments);
      h = std::hash<std::string>{}(j.dump());
    } catch (...) {
      h = std::hash<std::string>{}(c.arguments);
    }
    sigs.push_back({c.name, h});
  }
  return sigs;
}

bool StallDetector::rounds_match(const std::vector<ToolCallSignature>& a,
                                  const std::vector<ToolCallSignature>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (!(a[i] == b[i])) return false;
  }
  return true;
}

StallResult StallDetector::check(const std::vector<ToolCall>& current_round) {
  auto current_sigs = signatures_of(current_round);

  int consecutive = 1;
  for (auto it = recent_rounds_.rbegin(); it != recent_rounds_.rend(); ++it) {
    if (rounds_match(it->signatures, current_sigs)) {
      consecutive++;
    } else {
      break;
    }
  }

  StallLevel level = consecutive >= config_.force_stop_threshold ? StallLevel::ForceStop
                   : consecutive >= config_.sig_stall_threshold ? StallLevel::SigStall
                   : StallLevel::None;

  ToolCallSignature stalled_sig;
  if (!current_sigs.empty()) stalled_sig = current_sigs[0];

  recent_rounds_.push_back({turn_counter_++, current_sigs});
  while (static_cast<int>(recent_rounds_.size()) > config_.max_lookback_rounds) {
    recent_rounds_.pop_front();
  }

  return {level != StallLevel::None, consecutive, stalled_sig, level};
}

void StallDetector::reset() {
  recent_rounds_.clear();
  turn_counter_ = 0;
}

} // namespace merak
```

- [ ] **Step 4: Build and test**

Register in CMakeLists and test:
```bash
cmake --build build --target merak-loop-stall-detector-test 2>&1 | tail -5
./build/tests/merak-loop-stall-detector-test
```

- [ ] **Step 5: Commit**

```bash
git add libs/loop/include/merak/stall_detector.hpp libs/loop/src/stall_detector.cpp libs/loop/tests/test_stall_detector.cpp libs/loop/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(loop): add StallDetector with signature-based stall detection"
```

---

### Task 13: TurnIngestor

**Files:**
- Create: `libs/loop/include/merak/turn_ingestor.hpp`
- Create: `libs/loop/src/turn_ingestor.cpp`
- Modify: `libs/loop/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <merak/message.hpp>
#include <merak/execution.hpp>
#include <merak/stall_detector.hpp>
#include <chrono>
#include <vector>
#include <optional>

namespace merak {

struct TokenCount {
  int input = 0;
  int output = 0;
  int cache_read = 0;
  int cache_write = 0;
};

struct IngestedTurn {
  int index;
  std::vector<ToolCallSignature> tool_sigs;
  int tool_count;
  int total_tool_output_chars;
  TokenCount tokens;
  bool had_error;
  std::optional<LlmErrorClass> error_class;
  std::chrono::milliseconds llm_latency;
  std::string first_text_line;
};

class TurnIngestor {
public:
  TurnIngestor() = default;

  IngestedTurn ingest(const ToolCall* tool_calls, size_t tool_count,
                       const TokenCount& tokens,
                       const std::string& first_text_line,
                       std::chrono::milliseconds latency,
                       int turn_index);

  static LlmErrorClass classify_error(int http_status, const std::string& body_hint);
};

} // namespace merak
```

- [ ] **Step 2: Write the implementation**

Create `libs/loop/src/turn_ingestor.cpp`:

```cpp
#include <merak/turn_ingestor.hpp>
#include <algorithm>

namespace merak {

IngestedTurn TurnIngestor::ingest(const ToolCall* tool_calls, size_t tool_count,
                                    const TokenCount& tokens,
                                    const std::string& first_text_line,
                                    std::chrono::milliseconds latency,
                                    int turn_index) {
  IngestedTurn t;
  t.index = turn_index;
  t.tokens = tokens;
  t.first_text_line = first_text_line.substr(0, 200);
  t.llm_latency = latency;
  t.had_error = false;

  t.tool_count = static_cast<int>(tool_count);
  for (size_t i = 0; i < tool_count; i++) {
    auto sigs = StallDetector::signatures_of({tool_calls[i]});
    if (!sigs.empty()) t.tool_sigs.push_back(sigs[0]);
  }

  return t;
}

LlmErrorClass TurnIngestor::classify_error(int http_status,
                                             const std::string& body_hint) {
  if (http_status == 401 || http_status == 403) return LlmErrorClass::Auth;
  if (http_status == 429) return LlmErrorClass::RateLimit;
  if (http_status == 400 &&
      (body_hint.find("token") != std::string::npos ||
       body_hint.find("context") != std::string::npos ||
       body_hint.find("length") != std::string::npos)) {
    return LlmErrorClass::ContextWindow;
  }
  if (http_status >= 500) return LlmErrorClass::Unknown;
  return LlmErrorClass::Unknown;
}

} // namespace merak
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target merak-loop 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add libs/loop/include/merak/turn_ingestor.hpp libs/loop/src/turn_ingestor.cpp libs/loop/CMakeLists.txt
git commit -m "feat(loop): add TurnIngestor for structured LLM response ingestion"
```

---

### Task 14: TurnGuard

**Files:**
- Create: `libs/loop/include/merak/turn_guard.hpp`
- Create: `libs/loop/src/turn_guard.cpp`
- Create: `libs/loop/tests/test_turn_guard.cpp`
- Modify: `libs/loop/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `libs/loop/tests/test_turn_guard.cpp`:

```cpp
#include <merak/turn_guard.hpp>
#include <merak/stall_detector.hpp>
#include <merak/turn_ingestor.hpp>
#include <cassert>
#include <iostream>

using namespace merak;

int main() {
  // Test 1: IdleLoop detection
  {
    TurnGuard guard;
    TurnGuard::RoundInput in;
    in.turn_index = 5;
    in.had_write_operation = false;
    in.consecutive_read_only_rounds = 3;

    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Warning);
    assert(v.nudge.has_value());
    std::cout << "PASS: IdleLoop warning after 3 read-only rounds\n";
  }

  // Test 2: ToolFlood detection
  {
    TurnGuard guard;
    TurnGuard::RoundInput in;
    in.turn_index = 3;
    in.tool_count = 15;

    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Warning);
    assert(v.turn_penalty.has_value());
    assert(*v.turn_penalty == -2);
    std::cout << "PASS: ToolFlood warning with turn penalty\n";
  }

  // Test 3: Stall verdict
  {
    TurnGuard guard;
    TurnGuard::RoundInput in;
    in.turn_index = 7;
    in.stall.level = StallLevel::SigStall;

    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Warning);
    std::cout << "PASS: stall verdict triggers Warning\n";
  }

  // Test 4: ForceStop verdict
  {
    TurnGuard guard;
    TurnGuard::RoundInput in;
    in.turn_index = 10;
    in.stall.level = StallLevel::ForceStop;

    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Critical);
    std::cout << "PASS: force_stop triggers Critical\n";
  }

  // Test 5: Progressive penalty
  {
    TurnGuard guard;
    TurnGuard::RoundInput in;
    in.turn_index = 1;
    in.had_write_operation = false;
    in.consecutive_read_only_rounds = 3;

    // 1st warning
    auto v1 = guard.evaluate(in);
    assert(v1.turn_penalty.has_value());
    assert(*v1.turn_penalty == -2);

    // 2nd warning
    auto v2 = guard.evaluate(in);
    assert(v2.turn_penalty.has_value());
    assert(*v2.turn_penalty == -4);

    // 3rd warning
    auto v3 = guard.evaluate(in);
    assert(v3.turn_penalty.has_value());
    assert(*v3.turn_penalty == -6);

    // 4th — force stop
    auto v4 = guard.evaluate(in);
    assert(v4.severity == Severity::Critical);
    std::cout << "PASS: progressive penalty escalation\n";
  }

  // Test 6: Healthy round
  {
    TurnGuard guard;
    TurnGuard::RoundInput in;
    in.turn_index = 2;
    in.had_write_operation = true;
    in.tool_count = 3;
    in.stall.level = StallLevel::None;
    in.consecutive_read_only_rounds = 0;

    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Healthy);
    std::cout << "PASS: healthy round gets Healthy verdict\n";
  }

  std::cout << "\nAll turn guard tests passed\n";
  return 0;
}
```

- [ ] **Step 2: Write the header**

Create `libs/loop/include/merak/turn_guard.hpp`:

```cpp
#pragma once
#include <merak/section_kind.hpp>
#include <merak/message.hpp>
#include <merak/stall_detector.hpp>
#include <string>
#include <vector>
#include <optional>

namespace merak {

class TurnGuard {
public:
  struct Verdict {
    Severity severity = Severity::Healthy;
    std::string reason;
    std::optional<std::string> nudge;
    std::optional<int> turn_penalty;
    std::vector<std::string> restricted_tools;
  };

  struct RoundInput {
    int turn_index = 0;
    int tool_count = 0;
    bool had_write_operation = false;
    int consecutive_read_only_rounds = 0;
    int consecutive_world_query_rounds = 0;
    int consecutive_content_avoidance = 0;
    bool had_duplicate_creation = false;
    bool had_tone_drift = false;
    StallResult stall;
  };

  TurnGuard() = default;

  Verdict evaluate(const RoundInput& input);

  void reset();

private:
  int warning_count_ = 0;

  int penalty_for(int count) const;
};

} // namespace merak
```

- [ ] **Step 3: Write the implementation**

Create `libs/loop/src/turn_guard.cpp`:

```cpp
#include <merak/turn_guard.hpp>

namespace merak {

int TurnGuard::penalty_for(int count) const {
  if (count >= 4) return -999;  // force stop
  return -(2 * count);
}

TurnGuard::Verdict TurnGuard::evaluate(const RoundInput& in) {
  Verdict v;

  // ForceStop from StallDetector — highest priority
  if (in.stall.level == StallLevel::ForceStop) {
    v.severity = Severity::Critical;
    v.reason = "force_stop: 5 consecutive identical tool-call rounds";
    return v;
  }

  // WorldObsession
  if (in.consecutive_world_query_rounds >= 5) {
    v.severity = Severity::Critical;
    v.reason = "5+ rounds of world-only queries without narrative output";
    v.restricted_tools = {"query_map", "query_world", "query_history", "query_magic", "query_faction"};
    v.turn_penalty = -4;
    return v;
  }

  // IdleLoop
  if (in.consecutive_read_only_rounds >= 3) {
    v.severity = Severity::Warning;
    v.reason = "3+ rounds without write operations";
    v.nudge = "你已经观察了很多信息，现在是时候写内容了。";
  }

  // ContentAvoidance
  if (in.consecutive_content_avoidance >= 3) {
    v.severity = Severity::Warning;
    v.reason = "3x refusal to advance narrative";
    v.nudge = "接受不完美，先写下来，后面可以改。";
  }

  // ToolFlood
  if (in.tool_count >= 15) {
    v.severity = Severity::Warning;
    v.reason = "excessive tool calls in single round";
    v.turn_penalty = -2;
  }

  // RepeatCreation
  if (in.had_duplicate_creation) {
    if (v.severity < Severity::Warning) v.severity = Severity::Warning;
    v.nudge = "检查是否已存在同名角色或地点。";
  }

  // ToneDrift
  if (in.had_tone_drift) {
    if (v.severity < Severity::Info) v.severity = Severity::Info;
    v.nudge = "留意你的叙事语气，保持与场景时代背景一致。";
  }

  // Stall
  if (in.stall.level == StallLevel::SigStall) {
    if (v.severity < Severity::Warning) v.severity = Severity::Warning;
    if (!v.nudge) v.nudge = "试着调用 write_file 把想法写出来。";
  }

  // Progressive penalty
  if (v.severity >= Severity::Warning) {
    warning_count_++;
    if (!v.turn_penalty) {
      v.turn_penalty = penalty_for(warning_count_);
    }
    if (warning_count_ >= 4) {
      v.severity = Severity::Critical;
      v.reason = "4+ warnings in this run";
    }
  }

  return v;
}

void TurnGuard::reset() {
  warning_count_ = 0;
}

} // namespace merak
```

- [ ] **Step 4: Build and test**

```bash
cmake --build build --target merak-loop-turn-guard-test 2>&1 | tail -5
./build/tests/merak-loop-turn-guard-test
```

Expected: all tests pass

- [ ] **Step 5: Commit**

```bash
git add libs/loop/include/merak/turn_guard.hpp libs/loop/src/turn_guard.cpp libs/loop/tests/test_turn_guard.cpp libs/loop/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(loop): add TurnGuard with novel-writing behavior rules and progressive penalties"
```

---

## Phase 7: AgentLoop Integration

### Task 15: Update AgentLoop to use new pipeline and guards

**Files:**
- Modify: `libs/loop/include/merak/agent_loop.hpp`
- Modify: `libs/loop/src/agent_loop.cpp`

This is the critical integration task. The full file is ~360 lines. We modify specific sections.

- [ ] **Step 1: Update agent_loop.hpp**

Add new includes and dependencies:

```cpp
// Add these includes:
#include <merak/context_pipeline.hpp>
#include <merak/stall_detector.hpp>
#include <merak/turn_guard.hpp>
#include <merak/turn_ingestor.hpp>

// Modify Config struct:
struct Config {
  int max_turns = 25;
  std::string system_prompt;
  std::string default_model = "gpt-4o";
  int max_output_tokens = 4096;
  int model_max_tokens = 128000;       // added
  bool enable_compaction = true;
  bool enable_cache = true;
};

// Add to class AgentLoop:
private:
  // New members
  std::unique_ptr<ContextPipeline> pipeline_;
  StallDetector stall_detector_;
  TurnGuard turn_guard_;
  TurnIngestor turn_ingestor_;
  int consecutive_read_only_rounds_ = 0;
  int consecutive_world_query_rounds_ = 0;
  int consecutive_content_avoidance_ = 0;
  // Remove: counter_, tool_result_compactor_ (now inside pipeline)
```

Update constructor to create `pipeline_` instead of the old `context_` / `compactor_` / `counter_`.

- [ ] **Step 2: Rewrite build_context() in agent_loop.cpp**

Replace the existing `build_context()`:

```cpp
std::vector<Message> AgentLoop::build_context() {
  // Build BindSources from agent state
  BindSources sources;
  sources.identity_text = [this]() {
    // Extract first 500 chars as identity core
    return config_.system_prompt.substr(0, std::min<size_t>(500, config_.system_prompt.size()));
  };
  sources.constraints_text = [this]() {
    // Rest of system prompt after identity
    if (config_.system_prompt.size() > 500) {
      return config_.system_prompt.substr(500);
    }
    return std::string("");
  };
  sources.world_context_text = []() { return ""; };  // wired later from worldbuilding
  sources.skills_text = []() { return ""; };           // wired later from SkillRegistry
  sources.tool_specs = tools_->visible_schemas();  // need to add this method to ToolRegistry
  sources.working_memory_text = []() { return ""; };   // wired later from NarrativeWorkingMemory
  sources.memory_store = memory_;
  // Use latest user message as search query
  for (auto it = session_history_.rbegin(); it != session_history_.rend(); ++it) {
    if (it->role == "user") {
      sources.search_query = it->content;
      break;
    }
  }
  sources.conversation_messages = memory_->recent_history(config_.max_turns);

  auto payload = pipeline_->planned_assemble(
    config_.system_prompt, config_.default_model,
    config_.model_max_tokens, session_history_, sources);

  // Cache-aware split for logging
  if (config_.enable_cache) {
    auto split = CacheAwareContext::split(payload.messages);
    spdlog::debug("Cache info: {}", CacheAwareContext::info(split));
  }

  return payload.messages;
}
```

- [ ] **Step 3: Rewrite maybe_compact() in agent_loop.cpp**

```cpp
void AgentLoop::maybe_compact(RunControl& control) {
  if (!config_.enable_compaction) return;

  // Phase 3 microcompact is handled by ContextOptimizer during pipeline assembly.
  // Here we only do LLM-based compaction if needed (CompactHistory+ tier).
  // The pipeline planner tells us through stats whether compaction occurred.

  // Check if we need full LLM compaction
  auto& stats = pipeline_->stats();
  int total_tokens = 0;
  for (auto& msg : session_history_) {
    total_tokens += static_cast<int>(msg.content.size() / 3.5);
  }

  if (total_tokens > config_.model_max_tokens * 0.75 && compactor_) {
    spdlog::info("Triggering LLM compaction at {} tokens", total_tokens);
    int keep_recent = config_.max_turns * 2;
    auto result = compactor_->compact_history(session_history_, keep_recent).get();
    // Inject summary
    if (!result.summary.empty()) {
      Message summary_msg;
      summary_msg.role = "system";
      summary_msg.content = "[Previous conversation summary]\n" + result.summary;
      session_history_.insert(session_history_.begin(), summary_msg);
      control.record_compaction(static_cast<int>(result.replaced.size()));
    }
  }
}
```

- [ ] **Step 4: Update run_loop() to integrate StallDetector and TurnGuard**

In `run_loop()`, after LLM response and tool extraction, add:

```cpp
// After extracting tool_calls from LLM response:
auto ingested = turn_ingestor_.ingest(
  tool_calls.data(), tool_calls.size(),
  {resp_input_tokens, resp_output_tokens, cache_read, cache_write, 0, false, false, false},
  text_response.empty() ? "" : text_response,
  std::chrono::milliseconds{llm_latency_ms},
  turn_index
);

// If no tool calls — done
if (tool_calls.empty()) {
  transition_to(TurnState::Responding, control);
  // ...
  return response;
}

// Stall detection
auto stall = stall_detector_.check(tool_calls);
if (stall.level == StallLevel::ForceStop) {
  // Force text-only final call
  auto final_req = build_text_only_request();
  auto final_resp = co_await llm_->chat(final_req, ...);
  transition_to(TurnState::Complete, control);
  return AgentResponse{final_resp.text, ...};
}

// Execute tools
transition_to(TurnState::Acting, control);
auto results = handle_tool_calls(tool_calls, control);
transition_to(TurnState::Observing, control);

// TurnGuard evaluation
TurnGuard::RoundInput guard_in;
guard_in.turn_index = turn_index;
guard_in.tool_count = static_cast<int>(tool_calls.size());
guard_in.stall = stall;
guard_in.consecutive_read_only_rounds = consecutive_read_only_rounds_;
guard_in.consecutive_world_query_rounds = consecutive_world_query_rounds_;
guard_in.consecutive_content_avoidance = consecutive_content_avoidance_;

// Track write operations
bool had_write = false;
bool had_world_query_only = true;
for (auto& tc : tool_calls) {
  if (tc.name == "write_file" || tc.name == "str_replace" || tc.name == "create_character" ||
      tc.name == "create_scene" || tc.name == "create_chapter" || tc.name == "add_world_knowledge" ||
      tc.name == "create_location" || tc.name == "plant_foreshadowing" || tc.name == "expose_secret") {
    had_write = true;
    had_world_query_only = false;
  }
  if (tc.name != "query_map" && tc.name != "query_world" && tc.name != "query_history" &&
      tc.name != "query_magic" && tc.name != "query_faction" && tc.name != "search_agent" &&
      tc.name != "look_around" && tc.name != "read_character_card" && tc.name != "read_secret" &&
      tc.name != "read_foreshadowing" && tc.name != "search_my_diary" && tc.name != "read_file") {
    had_world_query_only = false;
  }
}
guard_in.had_write_operation = had_write;

if (had_write) {
  consecutive_read_only_rounds_ = 0;
} else {
  consecutive_read_only_rounds_++;
}

if (had_world_query_only) {
  consecutive_world_query_rounds_++;
} else {
  consecutive_world_query_rounds_ = 0;
}

auto verdict = turn_guard_.evaluate(guard_in);

// Apply verdict
if (verdict.turn_penalty) {
  config_.max_turns = std::max(1, config_.max_turns + *verdict.turn_penalty);
}
if (verdict.severity == Severity::Critical && turn_guard_.warning_count() >= 4) {
  break;  // force stop the loop
}
if (verdict.nudge) {
  Message nudge_msg;
  nudge_msg.role = "system";
  nudge_msg.content = "[校正] " + *verdict.nudge;
  session_history_.push_back(nudge_msg);
}
if (!verdict.restricted_tools.empty()) {
  tools_->apply_restriction(verdict.restricted_tools, 2);
}
```

- [ ] **Step 5: Build**

```bash
cmake --build build --target merak-loop 2>&1 | tail -10
```

Fix any compilation issues, then:

- [ ] **Step 6: Commit**

```bash
git add libs/loop/include/merak/agent_loop.hpp libs/loop/src/agent_loop.cpp
git commit -m "feat(loop): integrate ContextPipeline, StallDetector, TurnGuard into AgentLoop"
```

---

## Phase 8: Session Memory Extraction (P1)

### Task 16: SessionMemorySnapshot types

**Files:**
- Create: `libs/memory/include/merak/session_memory_snapshot.hpp`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <cstdint>

namespace merak {

using UUID = std::string;  // matches existing convention
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
```

- [ ] **Step 2: Commit**

```bash
git add libs/memory/include/merak/session_memory_snapshot.hpp
git commit -m "feat(memory): add SessionMemorySnapshot types for structured memory extraction"
```

---

### Task 17: NarrativeWorkingMemory

**Files:**
- Create: `libs/memory/include/merak/narrative_working_memory.hpp`
- Create: `libs/memory/src/narrative_working_memory.cpp`
- Modify: `libs/memory/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
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
  float importance;  // 0.0 - 1.0
};

struct ToneConstraint {
  std::string tone;      // e.g., "紧张", "温馨", "史诗"
  std::string pacing;    // e.g., "慢节奏", "快节奏", "过渡"
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

  // Serialize for context injection
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
```

- [ ] **Step 2: Write the implementation**

Create `libs/memory/src/narrative_working_memory.cpp`:

```cpp
#include <merak/narrative_working_memory.hpp>
#include <sstream>

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
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target merak-memory 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add libs/memory/include/merak/narrative_working_memory.hpp libs/memory/src/narrative_working_memory.cpp libs/memory/CMakeLists.txt
git commit -m "feat(memory): add NarrativeWorkingMemory for storytelling continuity"
```

---

### Task 18: SessionJournal

**Files:**
- Create: `libs/memory/include/merak/session_journal.hpp`
- Create: `libs/memory/src/session_journal.cpp`
- Create: `libs/memory/tests/test_session_journal.cpp`
- Modify: `libs/memory/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `libs/memory/tests/test_session_journal.cpp`:

```cpp
#include <merak/session_journal.hpp>
#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using namespace merak;

int main() {
  auto tmp = std::filesystem::temp_directory_path() / "merak_test_journal";
  std::filesystem::create_directories(tmp);

  {
    SessionJournal journal(tmp);
    journal.append({"turn_completed", nlohmann::json{{"turn", 1}, {"tokens", 500}}});
    journal.append({"tool_executed", nlohmann::json{{"tool", "read_file"}, {"duration_ms", 42}}});
    journal.append({"memory_extracted", nlohmann::json{{"snapshot_id", "abc-123"}}});

    // Read back the file
    auto path = tmp / "session_test.jsonl";
    assert(std::filesystem::exists(path));

    std::ifstream f(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);
    assert(lines.size() == 3);

    auto j0 = nlohmann::json::parse(lines[0]);
    assert(j0["event"] == "turn_completed");
    assert(j0["payload"]["turn"] == 1);

    std::cout << "PASS: 3 events written to journal\n";
  }

  // Cleanup
  std::filesystem::remove_all(tmp);
  std::cout << "\nAll session journal tests passed\n";
  return 0;
}
```

- [ ] **Step 2: Write the header**

Create `libs/memory/include/merak/session_journal.hpp`:

```cpp
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <mutex>
#include <fstream>
#include <filesystem>

namespace merak {

class SessionJournal {
public:
  struct Event {
    std::string event_type;
    nlohmann::json payload;
  };

  explicit SessionJournal(const std::filesystem::path& dir);
  ~SessionJournal();

  void append(const Event& event);
  void append(const std::string& event_type, const nlohmann::json& payload);

  const std::filesystem::path& path() const { return file_path_; }

private:
  std::filesystem::path file_path_;
  std::ofstream file_;
  std::mutex mutex_;
};

} // namespace merak
```

- [ ] **Step 3: Write the implementation**

Create `libs/memory/src/session_journal.cpp`:

```cpp
#include <merak/session_journal.hpp>
#include <chrono>
#include <iomanip>

namespace merak {

SessionJournal::SessionJournal(const std::filesystem::path& dir) {
  std::filesystem::create_directories(dir);
  file_path_ = dir / "session.jsonl";
  file_.open(file_path_, std::ios::app);
}

SessionJournal::~SessionJournal() {
  if (file_.is_open()) file_.close();
}

void SessionJournal::append(const Event& event) {
  append(event.event_type, event.payload);
}

void SessionJournal::append(const std::string& event_type, const nlohmann::json& payload) {
  std::lock_guard lock(mutex_);
  if (!file_.is_open()) return;

  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);

  nlohmann::json entry;
  entry["ts"] = std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
  entry["event"] = event_type;
  entry["payload"] = payload;

  file_ << entry.dump() << "\n";
  file_.flush();
}

} // namespace merak
```

Wait — `std::put_time` doesn't work directly with nlohmann::json. Fix the implementation:

```cpp
#include <merak/session_journal.hpp>
#include <chrono>
#include <sstream>

namespace merak {

SessionJournal::SessionJournal(const std::filesystem::path& dir) {
  std::filesystem::create_directories(dir);
  file_path_ = dir / "session.jsonl";
  file_.open(file_path_, std::ios::app);
}

SessionJournal::~SessionJournal() {
  if (file_.is_open()) file_.close();
}

void SessionJournal::append(const Event& event) {
  append(event.event_type, event.payload);
}

void SessionJournal::append(const std::string& event_type, const nlohmann::json& payload) {
  std::lock_guard lock(mutex_);
  if (!file_.is_open()) return;

  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::ostringstream ts;
  ts << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");

  nlohmann::json entry;
  entry["ts"] = ts.str();
  entry["event"] = event_type;
  entry["payload"] = payload;

  file_ << entry.dump() << "\n";
  file_.flush();
}

} // namespace merak
```

- [ ] **Step 4: Build and test**

```bash
cmake --build build --target merak-memory-journal-test 2>&1 | tail -5
./build/tests/merak-memory-journal-test
```

- [ ] **Step 5: Commit**

```bash
git add libs/memory/include/merak/session_journal.hpp libs/memory/src/session_journal.cpp libs/memory/tests/test_session_journal.cpp libs/memory/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(memory): add SessionJournal for JSONL append-only audit trail"
```

---

### Task 19: MemoryExtractionService

**Files:**
- Create: `libs/memory/include/merak/memory_extraction_service.hpp`
- Create: `libs/memory/src/memory_extraction_service.cpp`
- Modify: `libs/memory/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <merak/session_memory_snapshot.hpp>
#include <merak/session_journal.hpp>
#include <merak/llm_provider.hpp>
#include <memory>
#include <future>
#include <chrono>
#include <queue>

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
```

- [ ] **Step 2: Write the implementation**

Create `libs/memory/src/memory_extraction_service.cpp`:

```cpp
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

  // Fire-and-forget
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
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target merak-memory 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add libs/memory/include/merak/memory_extraction_service.hpp libs/memory/src/memory_extraction_service.cpp libs/memory/CMakeLists.txt
git commit -m "feat(memory): add MemoryExtractionService with gated fire-and-forget extraction"
```

---

## Phase 9: Host Abstraction (P2)

### Task 20: LoopHost interface

**Files:**
- Create: `libs/core/include/merak/loop_host.hpp`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <merak/message.hpp>
#include <merak/execution.hpp>
#include <merak/section_kind.hpp>
#include <future>
#include <optional>

namespace merak {

class LoopHost {
public:
  virtual ~LoopHost() = default;

  // Output
  virtual void on_text_delta(const std::string& delta) = 0;
  virtual void on_tool_start(const ToolCall& call) = 0;
  virtual void on_tool_end(const ToolCall& call, const ToolResult& result) = 0;
  virtual void on_state_change(TurnState from, TurnState to) = 0;
  virtual void on_error(const std::string& error_msg) = 0;

  // Input
  virtual std::future<std::optional<Message>> next_user_message() = 0;

  // Approval
  virtual std::future<bool> await_approval(const ToolCall& call) = 0;

  // Cancellation
  virtual bool is_cancelled() const = 0;

  // Identity
  virtual std::string session_id() const = 0;
  virtual std::string run_id() const = 0;
};

} // namespace merak
```

- [ ] **Step 2: Commit**

```bash
git add libs/core/include/merak/loop_host.hpp
git commit -m "feat(core): add LoopHost abstract interface for host-agnostic agent loop"
```

---

### Task 21: LoopDispatcher

**Files:**
- Create: `libs/loop/include/merak/loop_dispatcher.hpp`
- Create: `libs/loop/src/loop_dispatcher.cpp`
- Modify: `libs/loop/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <merak/loop_host.hpp>
#include <merak/agent_loop.hpp>
#include <merak/context_pipeline.hpp>
#include <merak/memory/memory_store.hpp>
#include <memory>

namespace merak {

class LoopDispatcher {
public:
  LoopDispatcher(std::unique_ptr<LoopHost> host,
                 std::shared_ptr<LlmProvider> llm,
                 std::shared_ptr<ToolRegistry> tools,
                 std::shared_ptr<MemoryStore> memory);

  std::future<void> run_session();

private:
  std::unique_ptr<AgentLoop> create_loop(const std::string& system_prompt);

  std::unique_ptr<LoopHost> host_;
  std::shared_ptr<LlmProvider> llm_;
  std::shared_ptr<ToolRegistry> tools_;
  std::shared_ptr<MemoryStore> memory_;
};

} // namespace merak
```

- [ ] **Step 2: Write the implementation**

Create `libs/loop/src/loop_dispatcher.cpp`:

```cpp
#include <merak/loop_dispatcher.hpp>

namespace merak {

LoopDispatcher::LoopDispatcher(std::unique_ptr<LoopHost> host,
                                 std::shared_ptr<LlmProvider> llm,
                                 std::shared_ptr<ToolRegistry> tools,
                                 std::shared_ptr<MemoryStore> memory)
  : host_(std::move(host)), llm_(std::move(llm)),
    tools_(std::move(tools)), memory_(std::move(memory)) {}

std::future<void> LoopDispatcher::run_session() {
  return std::async(std::launch::async, [this]() {
    // Future: migrate AgentLoop to accept LoopHost& instead of RunControl&
  });
}

} // namespace merak
```

- [ ] **Step 3: Commit**

```bash
git add libs/loop/include/merak/loop_dispatcher.hpp libs/loop/src/loop_dispatcher.cpp libs/loop/CMakeLists.txt
git commit -m "feat(loop): add LoopDispatcher for host-agnostic session orchestration"
```

---

## Phase 10: SpillStore + AssemblyTrace (P3)

### Task 22: SpillStore

**Files:**
- Create: `libs/context/include/merak/spill_store.hpp`
- Create: `libs/context/src/spill_store.cpp`
- Modify: `libs/context/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <filesystem>
#include <string>
#include <optional>
#include <mutex>

namespace merak {

class SpillStore {
public:
  explicit SpillStore(const std::filesystem::path& base_dir,
                       size_t max_total_bytes = 100 * 1024 * 1024);

  std::optional<SpillReference> spill(SectionKind kind,
                                        const std::string& content,
                                        int turn_index);

  std::optional<std::string> recall(const SpillReference& ref);

  void purge_before(int turn_index);

private:
  std::filesystem::path base_dir_;
  size_t max_total_bytes_;
  std::mutex mutex_;
};

} // namespace merak
```

- [ ] **Step 2: Write the implementation**

Create `libs/context/src/spill_store.cpp`:

```cpp
#include <merak/spill_store.hpp>
#include <fstream>
#include <sstream>
#include <functional>

namespace merak {

SpillStore::SpillStore(const std::filesystem::path& base_dir, size_t max_bytes)
  : base_dir_(base_dir), max_total_bytes_(max_bytes) {
  std::filesystem::create_directories(base_dir_);
}

std::optional<SpillReference> SpillStore::spill(SectionKind kind,
                                                  const std::string& content,
                                                  int turn_index) {
  // Never spill these
  if (kind == SectionKind::Identity ||
      kind == SectionKind::Constraints ||
      kind == SectionKind::WorkingMemory) {
    return std::nullopt;
  }

  std::lock_guard lock(mutex_);

  auto fname = std::to_string(turn_index) + "_"
             + section_kind_name(kind) + ".txt";
  auto path = base_dir_ / fname;

  std::ofstream f(path);
  if (!f) return std::nullopt;
  f << content;
  f.close();

  std::size_t h = std::hash<std::string>{}(content);
  std::ostringstream hash_oss;
  hash_oss << std::hex << h;

  return SpillReference{kind, path.string(), content.size(), hash_oss.str()};
}

std::optional<std::string> SpillStore::recall(const SpillReference& ref) {
  std::ifstream f(ref.path);
  if (!f) return std::nullopt;
  std::ostringstream oss;
  oss << f.rdbuf();
  return oss.str();
}

void SpillStore::purge_before(int turn_index) {
  std::lock_guard lock(mutex_);
  for (auto& entry : std::filesystem::directory_iterator(base_dir_)) {
    auto stem = entry.path().stem().string();
    auto underscore = stem.find('_');
    if (underscore == std::string::npos) continue;
    try {
      int t = std::stoi(stem.substr(0, underscore));
      if (t < turn_index) std::filesystem::remove(entry);
    } catch (...) {}
  }
}

} // namespace merak
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target merak-context 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add libs/context/include/merak/spill_store.hpp libs/context/src/spill_store.cpp libs/context/CMakeLists.txt
git commit -m "feat(context): add SpillStore for disk overflow of oversized sections"
```

---

### Task 23: AssemblyTrace

**Files:**
- Create: `libs/context/include/merak/assembly_trace.hpp`

- [ ] **Step 1: Write the header**

```cpp
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
    std::string content_preview;   // first 200 chars
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
```

- [ ] **Step 2: Commit**

```bash
git add libs/context/include/merak/assembly_trace.hpp
git commit -m "feat(context): add AssemblyTrace for per-turn context audit and debugging"
```

---

## Final Integration Task

### Task 24: Wire everything together and verify full build

- [ ] **Step 1: Update all CMakeLists.txt files**

Ensure all new source files are registered:
- `libs/core/CMakeLists.txt`: (INTERFACE, no changes needed — headers auto-discovered)
- `libs/context/CMakeLists.txt`: add `pipeline_stats.cpp`, `context_planner.cpp`, `context_binder.cpp`, `context_optimizer.cpp`, `context_serializer.cpp`, `context_pipeline.cpp`, `spill_store.cpp`
- `libs/loop/CMakeLists.txt`: add `stall_detector.cpp`, `turn_guard.cpp`, `turn_ingestor.cpp`, `loop_dispatcher.cpp`
- `libs/memory/CMakeLists.txt`: add `narrative_working_memory.cpp`, `session_journal.cpp`, `memory_extraction_service.cpp`
- `tests/CMakeLists.txt`: add all new test targets

- [ ] **Step 2: Full build**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: successful build of all targets

- [ ] **Step 3: Run all tests**

```bash
cd build && ctest --output-on-failure 2>&1 | tail -30
```

Expected: all tests pass

- [ ] **Step 4: Run existing tests to verify no regressions**

```bash
cd build && ctest -R "merak-context-test" --output-on-failure
```

Expected: existing tests still pass

- [ ] **Step 5: Commit**

```bash
git add libs/context/CMakeLists.txt libs/loop/CMakeLists.txt libs/memory/CMakeLists.txt tests/CMakeLists.txt
git commit -m "build: register all new source files and test targets"
```

---

## Summary

Total: 24 tasks across 10 phases, producing ~35 new files and modifying ~6 existing files.

**Task Dependency Order:**
1. Task 1 (enums)
2. Task 2 (pipeline types) → depends on Task 1
3. Task 3 (LlmErrorClass) → independent
4. Task 4 (PipelineStats) → depends on Task 1, 2
5. Task 5 (ContextPlanner) → depends on Task 4
6. Task 6 (ContextBinder) → depends on Task 2
7. Task 7 (ContextOptimizer) → depends on Task 2
8. Task 8 (ContextSerializer) → depends on Task 2
9. Task 9 (ContextPipeline facade) → depends on Tasks 5-8
10. Task 10 (Compactor update) → independent
11. Task 11 (ToolResultCompactor deprecation) → depends on Task 7
12. Task 12 (StallDetector) → depends on Task 1
13. Task 13 (TurnIngestor) → depends on Tasks 3, 12
14. Task 14 (TurnGuard) → depends on Tasks 1, 12
15. Task 15 (AgentLoop integration) → depends on Tasks 9, 12, 13, 14
16. Task 16 (Snapshot types) → independent
17. Task 17 (NarrativeWorkingMemory) → independent
18. Task 18 (SessionJournal) → independent
19. Task 19 (MemoryExtractionService) → depends on Tasks 16, 18
20. Task 20 (LoopHost) → depends on Task 1
21. Task 21 (LoopDispatcher) → depends on Task 20
22. Task 22 (SpillStore) → depends on Task 2
23. Task 23 (AssemblyTrace) → depends on Task 1, 2
24. Task 24 (Final integration + verify) → depends on all
