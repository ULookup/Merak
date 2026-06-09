# Context, Memory, and Agent Loop Upgrade Design

**Date:** 2026-06-09
**Reference:** Astra (`/home/icepop/astra`)
**Status:** design approved

## Overview

Upgrade Merak's context management, agent memory, and agent loop robustness from a
linear ad-hoc assembly to a structured multi-phase pipeline with production-grade
guardrails. The design is informed by astra's mature implementations, adapted and
specialized for Merak's novel-writing domain.

---

## 1. Architecture Overview

### Current (ad-hoc)

```
User Input → AgentLoop (maybe_compact → build_context → LLM → tool_round) → Response
```

### Target (pipelined)

```
User Input → AgentLoop:
  loop:
    1. ContextPipeline.planned_assemble(state)
    2. LLM call + streaming
    3. TurnIngestor.ingest(response)
    4. if tool_calls:
         StallDetector.check(tool_calls)
         ToolExecutor.execute_all(tool_calls)
         TurnGuard.evaluate(round_result)
       else: break
    5. MemoryExtractor.extract_async(turn)  [fire-and-forget]
  return response
```

### New/Upgraded Components

| Component | Status | Priority |
|-----------|--------|----------|
| ContextPipeline (Plan→Bind→Optimize→Serialize) | Rewrite | P0 |
| StallDetector | New | P1 |
| TurnGuard + TurnIngestor | New | P1 |
| MemoryExtractionService + SessionJournal + NarrativeWorkingMemory | New | P1 |
| LoopHost + LoopDispatcher | New | P2 |
| SpillStore + AssemblyTrace | New | P3 |

---

## 2. ContextPipeline — Four-Phase Pipeline (P0)

Replaces the ad-hoc context assembly currently spread across `agent_loop.cpp`,
`ContextAssembler`, `Compactor`, `ToolResultCompactor`, and `CacheAwareContext`.
Converges them into a deterministic per-turn pipeline: Plan → Bind → Optimize → Serialize.

### 2.1 Phase 1: Plan (pure computation, no I/O)

Computes pressure, selects compaction tier, allocates budget, builds section manifest.

**Pressure model:**

```
raw_pressure       = current_tokens / model_max
predictive_pressure = (current_tokens + output_reserve + thinking_reserve + schema_reserve) / model_max

output_reserve   = max(PipelineStats::response_tokens.p50, 4096)
thinking_reserve = max(PipelineStats::thinking_tokens.p50, 0)
schema_reserve   = tool_schemas.size() * avg_schema_tokens
```

**Gated tier selection (escalation only):**

```
base_tier = raw < 0.60 ? Normal
          : raw < 0.75 ? TrimSchemas
          : raw < 0.90 ? CompactHistory
          : AggressivePrune

tier = max(base_tier, predictive_tier)
if (prev_turn.context_window_error) tier = escalate_for_recovery(tier)
```

**Compaction tiers:**

| Tier | Raw Pressure | Actions |
|------|-------------|---------|
| Normal | < 0.60 | No compaction |
| TrimSchemas | >= 0.60 | Truncate tool descriptions to first sentence |
| CompactHistory | >= 0.75 | Microcompact tool results + LLM compact history |
| AggressivePrune | >= 0.90 | All above + drop oldest round pairs + spill |

**Adaptive budget allocation:**

Replaces static `reserve_ratio=0.15` / `memory_ratio=0.20` with EMA-based allocation
fed by `PipelineStats` from previous turns:

- Fixed sections (Identity, Constraints): capped at 2000 tokens each
- Adaptive sections: `min(historical_cap, EMA(observed_usage) * 1.5)`
- Memory budget shrinks with tier: 15% → 10% → 5%
- Conversation receives remaining budget (travels as provider messages)

**Section manifest ordering (cache-optimized):**

```
[Global scope — cacheable across sessions]
  Identity       — Agent role definition (core of system prompt)
  Constraints    — Hard rules, safety boundaries

[Session scope — cacheable within session]
  WorldContext   — Current scene, active characters, location, plot arcs
  Skills         — Loaded skill definitions
  ToolSchemas    — Visible tool JSON Schemas

[Turn scope — changes every turn, no caching]
  WorkingMemory  — Decisions, blockers, next actions (NEVER spilled)
  Memory         — Retrieved memory snippets

[Emergent scope]
  EmergentSkills   — Dynamically discovered skills
  EmergentMemory   — Newly formed memories this session
  EmergentSummary  — Compaction summaries
```

### 2.2 Phase 2: Bind (resolve to concrete text)

Resolves each planned section to real text from data sources:

| Section | Data Source |
|---------|------------|
| Identity | `AgentConfig.system_prompt` core (role definition portion) |
| Constraints | `AgentConfig.system_prompt` constraint portion + RuntimePolicy |
| WorldContext | `WorldbuildingStore` — current scene card + active characters + pending foreshadowing |
| Skills | `SkillRegistry.loaded()` — name + description |
| ToolSchemas | `ToolRegistry.visible_schemas()` — full JSON Schema |
| WorkingMemory | `NarrativeWorkingMemory` — current beat, open beats, character states |
| Memory | `MemoryStore.search(latest_user_message, top_k)` — semantic retrieval |
| Conversation | `working_memory.recent_history()` — as Message objects, not inline text |
| Emergent* | Dynamic sources, budget=0 in normal turns |

Lazy binding: sections with budget=0 skip I/O.

**Domain specialization — WorldContext sub-layers:**

```
WorldContext (session scope, rebinds on scene change):
  CurrentScene       — location, time, present characters
  ActiveCharacters   — card summaries (name, appearance, personality, current mood/goal)
  SceneGoal          — narrative goal for this scene (from Chapter/Arc)
  PendingForeshadowing — unresolved foreshadowing (top 5 by importance)
  RelevantSecrets    — secrets held by present characters (permission-filtered)
  RecentDiary        — last N diary entries for this agent

TurnContext (turn scope, refreshed each turn):
  LastNarrativeBeat  — one sentence of what happened last turn
  EmotionalState     — scene emotional tone + key character moods
  PacingNote         — pacing hint ("slow down, this is pre-climax" / "transition, speed up")
```

### 2.3 Phase 3: Optimize (budget-fitting transformations)

Five independently-gated transformations, applied only when budget is exceeded:

**1. Reorder** (available at Normal+):
Group sections by CacheScope (Global → Session → Turn). Place Anthropic
`cache_control` breakpoint after last Session section. Split OpenAI system
message into primary (Global+Session) and dynamic (Turn).

**2. Schema Pruning** (TrimSchemas+):
Truncate tool descriptions to first sentence. At AggressivePrune: strip
descriptions entirely, keep only name + params skeleton.

**3. Microcompact** (CompactHistory+):
Preserve 6 most recent tool results. Truncate older tool result content
exceeding 8000 chars to `[result truncated: X bytes]`. Never delete messages.
Non-compactable tools: `bash`, `write_file`, `str_replace`, `multi_edit`,
`delete_file`, `skill`, `delegate`.

**4. Round Dropping** (AggressivePrune+):
Drop oldest user/assistant round pairs. Keep at least 4 rounds. Convert
dropped rounds to summary via `Compactor.compact_one_round()` and append
to EmergentSummary.

**5. Spill** (AggressivePrune + still over budget):
Persist oversized sections to `~/.merak/spill/<session_id>/<turn>_<section>.txt`.
Replace inline with `SpillReference{section_id, path, byte_count, content_hash}`.
Identity, Constraints, and WorkingMemory are **never spilled**.

### 2.4 Phase 4: Serialize

Converts optimized context to provider-native request format:

- **OpenAI:** system message (split primary/dynamic) + messages array + tools array
- **Anthropic:** system blocks + messages with `cache_control` markers at scope boundaries
- Cache breakpoints: one after Identity+Constraints, one after Session scope

### 2.5 PipelineStats — Feedback Loop

```cpp
struct PipelineStats {
  // EMA accumulators
  double response_tokens_p50;   // 50th percentile estimate
  double response_tokens_p90;   // 90th percentile estimate
  double thinking_tokens_p50;
  double cache_hit_ratio;
  
  // Per-section usage history (for adaptive budget)
  std::map<SectionKind, EmaEstimate> section_usage;
  
  void record(const ContextFeedback& feedback, const OptimizeStats& opt_stats);
};
```

### 2.6 Agent Loop Integration

Before:
```cpp
maybe_compact();
auto ctx = build_context();         // one-shot assembly
auto response = co_await llm_->chat(ctx);
```

After:
```cpp
auto plan = planner_.plan(state, stats_);          // Phase 1
auto bound = binder_.bind(plan, state, sources_);  // Phase 2
auto opt = optimizer_.optimize(bound, plan);       // Phase 3
auto payload = serializer_.serialize(opt);         // Phase 4
auto feedback = co_await llm_->chat(payload);
stats_.record(feedback, opt.stats);                // close the loop
```

### 2.7 Files

| File | Op | Notes |
|------|-----|-------|
| `libs/context/include/merak/context_planner.hpp` | New | Phase 1 types + interface |
| `libs/context/src/context_planner.cpp` | New | Pressure, gated tier, budget, manifest |
| `libs/context/include/merak/context_binder.hpp` | New | Phase 2 interface |
| `libs/context/src/context_binder.cpp` | New | Section data source resolution |
| `libs/context/include/merak/context_optimizer.hpp` | New | Phase 3 interface |
| `libs/context/src/context_optimizer.cpp` | New | 5 transformations + spill |
| `libs/context/include/merak/context_serializer.hpp` | New | Phase 4 interface |
| `libs/context/src/context_serializer.cpp` | New | OpenAI/Anthropic format serialization |
| `libs/context/include/merak/pipeline_stats.hpp` | New | Cross-turn stats + EMA |
| `libs/context/src/pipeline_stats.cpp` | New | Stats accumulation + percentile estimation |
| `libs/context/include/merak/context_pipeline.hpp` | New | Four-phase orchestration facade |
| `libs/context/src/context_pipeline.cpp` | New | Orchestration implementation |
| `libs/context/include/merak/context_assembler.hpp` | Keep | Backward-compat, delegates to Pipeline |
| `libs/context/include/merak/compactor.hpp` | Modify | Add `compact_one_round()` for Round Dropping |
| `libs/context/include/merak/tool_result_compactor.hpp` | Modify | Migrate into Optimizer, keep compat |
| `libs/loop/src/agent_loop.cpp` | Modify | `build_context()` → `pipeline_.planned_assemble()` |

---

## 3. Agent Loop Hardening (P1)

### 3.1 StallDetector — Signature-Based Stall Detection

Detects repeated identical tool-call signatures. Orthogonal to the existing
circuit breaker (which handles execution failures, not repetition).

```cpp
struct ToolCallSignature {
  std::string tool_name;
  std::size_t args_hash;  // std::hash<normalized_json>(arguments)
};

struct StallResult {
  bool is_stalled;
  int consecutive_identical;
  ToolCallSignature stalled_signature;
  StallLevel level;  // None, SigStall(3), ForceStop(5)
};
```

**Algorithm:** sliding window over recent rounds. A signature is "repeated" only
if ALL tool calls in round N match ALL tool calls in round N-1. Non-consecutive
repetition resets the counter (ABAB is not a stall).

Stall at 3 → inject nudge. Stall at 5 → force text-only final call.

### 3.2 TurnGuard — Post-Tool Behavior Policy

Evaluates agent behavior after each tool round. Four severity levels with
progressive penalties:

| Level | Action |
|-------|--------|
| Healthy | Nothing |
| Info | Inject nudge into next round |
| Warning | Nudge + tool restriction + progressive turn penalty (-2/-4/-6) |
| Critical | Force text-only final call |

**Rules (novel-writing domain-specific):**

| Rule | Condition | Severity | Action |
|------|-----------|----------|--------|
| IdleLoop | 3+ rounds no write operations | Warning | Nudge: "观察够了，下笔写内容" |
| ContentAvoidance | LLM refuses to advance narrative 3x | Warning | Nudge: "接受不完美，先写再改" |
| ToolFlood | >=15 tool calls in single round | Warning | Restrict non-core tools; -2 turns |
| WorldObsession | 5+ rounds only world queries | Critical | Block world-query tools 2 rounds; -4 turns |
| RepeatCreation | Duplicate character/location name within short window | Warning | Nudge: "检查是否已存在" |
| ToneDrift | Modern slang in period setting (heuristic) | Info | Nudge: "注意叙事语气" |
| Stall | StallDetector reports SigStall | Warning | Inject tool suggestion |
| ForceStop | StallDetector reports ForceStop | Critical | Force text-only final |

**Progressive penalty:**

```
1st warning: -2 turns
2nd warning: -4 turns
3rd warning: -6 turns
4th+       : force stop (text-only final)
```

### 3.3 TurnIngestor — Structured LLM Response Ingestion

Extracts structured data from each LLM response for StallDetector, TurnGuard,
and PipelineStats consumption:

```cpp
struct IngestedTurn {
  TurnIndex index;
  std::vector<ToolCallSignature> tool_sigs;
  int tool_count;
  int total_tool_output_chars;
  TokenCount tokens;
  bool had_error;
  std::optional<LlmErrorClass> error_class;
  std::chrono::milliseconds llm_latency;
  std::string first_text_line;
};

enum class LlmErrorClass {
  None,
  ContextWindow,   // → escalate compaction tier
  RateLimit,       // → exponential backoff
  StreamIdle,      // → retry
  StreamTransport, // → retry
  Auth,            // → stop, no retry
  Cancelled,       // → stop
  Unknown,         // → retry once
};
```

### 3.4 Files

| File | Op | Notes |
|------|-----|-------|
| `libs/loop/include/merak/stall_detector.hpp` | New | Signature stall detection |
| `libs/loop/src/stall_detector.cpp` | New | Implementation |
| `libs/loop/include/merak/turn_guard.hpp` | New | Behavior verdict + progressive penalties |
| `libs/loop/src/turn_guard.cpp` | New | Rule implementations |
| `libs/loop/include/merak/turn_ingestor.hpp` | New | Response ingestion + error classification |
| `libs/loop/src/turn_ingestor.cpp` | New | Implementation |
| `libs/loop/src/agent_loop.cpp` | Modify | Integrate 3 new components |
| `libs/core/include/merak/execution.hpp` | Modify | Add `LlmErrorClass` enum |

---

## 4. Session Memory Extraction (P1)

### 4.1 SessionMemorySnapshot

```cpp
struct SessionMemorySnapshot {
  std::string schema_version = "1.0";
  UUID session_id;
  TurnIndex updated_turn;
  std::chrono::system_clock::time_point extracted_at;
  
  // Narrative layer
  std::string session_title;           // "酒馆对峙：Alice 揭露身份"
  std::string last_narrative_beat;     // one sentence of what happened this turn
  
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

struct ActiveGoal {
  std::string description;
  float progress;  // 0.0 - 1.0
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
  std::string field;  // "mood", "goal", "knowledge", "relationship"
  std::optional<std::string> old_value;
  std::string new_value;
};

struct ForeshadowingUpdate {
  UUID plant_id;
  std::string status;  // "planted", "advanced", "resolved", "abandoned"
  std::string note;
};
```

### 4.2 MemoryExtractionService

- Uses cheap model (gpt-4o-mini), 30s hard timeout, 4096 output tokens
- Fire-and-forget: fires from agent loop post-turn, does not block
- Gated: not every turn — only on creation events, secret/foreshadowing resolution,
  scene/chapter boundaries, user corrections, or every 5th turn
- Max 3 queued extractions to prevent backlog

### 4.3 Dual-Channel Memory Retrieval (Domain Specialization)

Two parallel retrieval channels replace single semantic search:

**Channel 1 — NarrativeMemory:** queries diary entries + scene summaries +
chapter events. Answers "what happened related to the current scene?" (top_k=3).

**Channel 2 — WorldFactMemory:** queries world_knowledge + character_sheets +
location_descriptions. Answers "what are the facts about [present characters /
location / magic system]?" (top_k=5).

Both channels feed into the Bind phase's Memory section, guaranteeing both
narrative continuity and world consistency.

### 4.4 NarrativeWorkingMemory (Domain Specialization)

Replaces the generic in-memory message buffer with narrative-structured state:

```cpp
struct NarrativeWorkingMemory {
  std::string last_beat;
  std::vector<std::string> open_beats;
  std::map<AgentId, CharacterMoment> character_states;
  std::vector<ForeshadowingRef> active_reminders;
  std::optional<ToneConstraint> tone;
};
```

Carries storytelling continuity (what's happening, what's unresolved, character
emotional states, foreshadowing obligations) — not just tool-level decisions.

### 4.5 Compression Awareness (Domain Specialization)

- **Narrative key-frame preservation:** identify and preserve turns where important
  narrative events occurred (foreshadowing planted, secrets exposed, character
  introductions). Compress non-key-frame detail first.
- **Natural compression boundaries:** leverage `end_scene` / `end_chapter` as
  aggressive compaction triggers. Scene boundaries are safe points to deeply
  compress history without losing narrative coherence.
- **Narrative-language summaries:** compaction summaries use storytelling language
  ("Alice登场，在酒馆遇到了旧友Bob，气氛紧张") not technical logs.

### 4.6 SessionJournal — JSONL Audit Trail

Append-only, one JSON line per event at `~/.merak/sessions/<session_id>.jsonl`.
Thread-safe writes. Events: turn completion, tool execution, compaction, memory
extraction, stall detection, verdict, config change, error.

### 4.7 Files

| File | Op | Notes |
|------|-----|-------|
| `libs/memory/include/merak/memory_extraction_service.hpp` | New | Extraction service + Config |
| `libs/memory/src/memory_extraction_service.cpp` | New | Gating + LLM extraction + persistence |
| `libs/memory/include/merak/session_memory_snapshot.hpp` | New | Snapshot type definitions |
| `libs/memory/include/merak/session_journal.hpp` | New | JSONL audit log |
| `libs/memory/src/session_journal.cpp` | New | Implementation |
| `libs/memory/include/merak/narrative_working_memory.hpp` | New | Narrative working memory structure |
| `libs/memory/src/narrative_working_memory.cpp` | New | Implementation |
| `libs/context/src/context_binder.cpp` | Modify | Dual-channel memory injection into Bind |

---

## 5. Host Abstraction (P2)

### 5.1 LoopHost Interface

Abstracts all host-specific I/O so the same loop code runs on CLI, HTTP/SSE,
and future hosts:

```cpp
class LoopHost {
public:
  virtual ~LoopHost() = default;
  
  // Output
  virtual void on_text_delta(const std::string& delta) = 0;
  virtual void on_tool_start(const ToolCall& call) = 0;
  virtual void on_tool_end(const ToolCall& call, const ToolResult& result) = 0;
  virtual void on_state_change(TurnState from, TurnState to) = 0;
  virtual void on_verdict(const TurnGuard::Verdict& verdict) = 0;
  virtual void on_stall(const StallDetector::StallResult& stall) = 0;
  virtual void on_error(const AgentError& error) = 0;
  
  // Input
  virtual future<std::optional<Message>> next_user_message() = 0;
  
  // Approval
  virtual future<ApprovalResult> await_approval(const ToolCall& call) = 0;
  virtual future<CreationResult> await_creation(const CreationPending& pending) = 0;
  
  // Cancellation
  virtual bool is_cancelled() const = 0;
  
  // Identity
  virtual SessionId session_id() const = 0;
  virtual RunId run_id() const = 0;
};
```

Two implementations:
- **CliLoopHost:** terminal I/O via FTXUI (spinners, approval prompts, Ctrl+C)
- **ServerLoopHost:** SSE streaming + HTTP-based approval ledger

### 5.2 LoopDispatcher

Unified entry point. Creates AgentLoop per run, sharing dependencies:

```cpp
future<void> LoopDispatcher::run_session() {
  while (!host_->is_cancelled()) {
    auto msg = co_await host_->next_user_message();
    if (!msg) break;
    auto loop = create_loop();
    loop->inject_message(*msg);
    auto response = co_await loop->run_loop(*host_);
  }
}
```

### 5.3 RunControl Simplification

Existing `RunControl` is simplified to `RunNotifier` — unidirectional
notification only (state changes, usage, checkpoints). Bidirectional
communication (approval, creation, input) moves to `LoopHost`.

### 5.4 Files

| File | Op | Notes |
|------|-----|-------|
| `libs/core/include/merak/loop_host.hpp` | New | LoopHost abstract interface |
| `libs/loop/include/merak/loop_dispatcher.hpp` | New | Unified dispatcher |
| `libs/loop/src/loop_dispatcher.cpp` | New | Implementation |
| `cli/src/cli_loop_host.cpp` | New | CLI host implementation |
| `libs/http/src/server_loop_host.cpp` | New | Server SSE host implementation |
| `libs/loop/src/agent_loop.cpp` | Modify | Remove ad-hoc I/O, route through host |
| `libs/core/include/merak/execution.hpp` | Modify | RunControl → RunNotifier |

---

## 6. Spill System + AssemblyTrace (P3)

### 6.1 SpillStore

When AggressivePrune is insufficient, persist oversized sections to
`~/.merak/spill/<session_id>/<turn>_<section>.txt`. Replace inline with
`SpillReference{section_id, path, byte_count, content_hash}`.

**Hard invariants:**
- Identity, Constraints, WorkingMemory are **never spilled**
- Total spill directory capped at 100 MB, oldest entries evicted first
- Agent can explicitly `read_file(spill_path)` to recall content

### 6.2 AssemblyTrace

Per-turn metadata recording what went into context, why, and at what token cost.
Written to SessionJournal, not into LLM context.

```cpp
struct AssemblyTrace {
  TurnIndex turn;
  CompactionTier tier;
  std::vector<SectionTrace> sections;      // per-section budget vs actual
  std::vector<OptimizerAction> actions;    // what transformations were applied
  int total_tokens_before, total_tokens_after, tokens_saved;
  float cache_hit_ratio;
};
```

Used for debugging ("why did token usage spike?"), optimization ("which section
is consistently under budget?"), and audit ("reconstruct the exact context
for turn N").

### 6.3 Files

| File | Op | Notes |
|------|-----|-------|
| `libs/context/include/merak/spill_store.hpp` | New | Spill storage interface |
| `libs/context/src/spill_store.cpp` | New | Disk spill implementation |
| `libs/context/include/merak/assembly_trace.hpp` | New | Trace type definitions |
| `libs/context/src/context_pipeline.cpp` | Modify | Integrate Spill in Optimize; produce Trace |

---

## 7. Summary

| # | Component | Priority | New Files | Modified Files |
|---|-----------|----------|-----------|----------------|
| 1 | ContextPipeline (4-phase) | P0 | 12 | 3 |
| 2 | StallDetector | P1 | 2 | 0 |
| 3 | TurnGuard + TurnIngestor | P1 | 4 | 1 |
| 4 | MemoryExtraction + Journal + NarrativeWM | P1 | 7 | 1 |
| 5 | LoopHost + LoopDispatcher | P2 | 7 | 1 |
| 6 | SpillStore + AssemblyTrace | P3 | 3 | 1 |
| **Total** | | | **~35 new** | **~6 unique modified** |

---

## 8. Domain Specializations Summary

These are the novel/worldbuilding-specific adaptations beyond generic agent
improvements:

1. **WorldContext sub-layers** (Section 2.2): Scene card + active characters +
   pending foreshadowing + secrets — granular world state binding
2. **TurnContext + NarrativeWorkingMemory** (Sections 2.2, 4.4): Narrative
   continuity tracking beyond tool-level decisions
3. **Dual-channel memory retrieval** (Section 4.3): NarrativeMemory +
   WorldFactMemory — separate channels for story continuity and world consistency
4. **Narrative-key-frame compression** (Section 4.5): Preserve important story
   beats; use scene boundaries as natural compaction triggers; summarize in
   storytelling language
5. **TurnGuard rules** (Section 3.2): IdleLoop, ContentAvoidance, WorldObsession,
   RepeatCreation, ToneDrift — guardrails for writing productivity, not just
   tool efficiency
6. **Narrative-phase tool visibility** (design discussion): Tools can be
   phase-filtered (outline/write/create/close) to reduce schema bloat
