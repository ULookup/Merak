# Provider Error Handling & Prompt Caching

**Date:** 2026-06-01
**Status:** approved
**Reference:** astra (https://github.com/ULookup/astra.git) — ErrorKind taxonomy, RateLimitCooldown, CacheScope patterns

---

## Part A: LLM Provider HTTP Error Handling

### Problem

Both `OpenAIProvider` and `AnthropicProvider` in `libs/llm/src/` check only for `CURLE_OK` after `curl_easy_perform()`. HTTP-level errors (429, 401, 403, 5xx) are invisible — the request "succeeds" at the curl layer but returns an empty or non-SSE body, producing silent failures (0 tokens). The `max_retries = 3` config field exists but is never used.

### Design

#### A1. ErrorKind Enum

New file: `libs/core/include/merak/error_kind.hpp`

```cpp
enum class ErrorKind {
    RateLimit,       // 429 / "rate" keyword
    Auth,            // 401, 403
    ServerError,     // 5xx, retryable
    ContextWindow,   // context_length_exceeded — do NOT retry
    InvalidRequest,  // 4xx other — do NOT retry
    StreamTimeout,   // idle timeout during streaming
    Network,         // connection/DNS/transport failure
    Unknown          // fallback
};
```

Classification function maps HTTP status code + response body keywords → ErrorKind.

#### A2. Retry Loop

Applied in both `OpenAIProvider::chat()` and `AnthropicProvider::chat()`:

```
1. curl_easy_perform() → CURLcode check
2. If OK: curl_easy_getinfo(CURLINFO_RESPONSE_CODE) → map to ErrorKind
3. If retryable (RateLimit/ServerError/Network) AND attempt < max_retries:
   - Parse Retry-After header for 429 (use server-specified delay)
   - Exponential backoff: 1s * 2^(attempt-1), max 30s
   - Log warning with attempt count
   - Retry from step 1
4. If non-retryable (Auth/ContextWindow/InvalidRequest): throw immediately
5. If all retries exhausted: throw with BudgetExhausted or classified error
6. Total budget guard: 30s wall-clock cap across all retry attempts
```

#### A3. Secrets Redaction

Before logging or propagating Auth errors, scan response body for `sk-`, `Bearer `, `key-` patterns and replace with `[REDACTED]`. The thrown error message must not leak API keys.

#### A4. Files Changed

| File | Change |
|------|--------|
| `libs/core/include/merak/error_kind.hpp` | **NEW** — ErrorKind enum + classify function |
| `libs/llm/src/openai_provider.cpp` | Add HTTP status check, retry loop, redaction |
| `libs/llm/src/anthropic_provider.cpp` | Same |

---

## Part B: Prompt Caching

### Problem

Caching infrastructure exists but is a skeleton: `CacheAwareContext::split()` computes static/dynamic prefixes but the result is only debug-logged. `ChatRequest::enable_cache` is ignored by both providers. `CacheStats` is defined but never updated beyond `total_requests++`. Users pay for the full context on every turn.

### Design

#### B1. CacheScope Classification

Extend `libs/context/include/merak/cache_aware_context.hpp`:

```cpp
enum class CacheScope { Global, Session, None };

struct ScopedBlock {
    CacheScope scope;
    std::string content;
};
```

- **Global** — never changes: core safety rules, role definition. Always in cache prefix.
- **Session** — stable within a session: CWD, date, project info. After Global, before breakpoint.
- **None** — per-turn volatile: memory snippets, user message, dynamic instructions. After breakpoint.

Ordering: `Global < Session < None` guarantees deterministic byte layout.

#### B2. ContextAssembler Changes

`assemble()` output splits into `cached_prefix` (Global + Session messages) and `dynamic_suffix` (None-scoped messages). `AgentLoop::build_context()` uses this split to pass cache breakpoint info to the provider.

#### B3. Anthropic Provider Implementation

In `build_request_body()`:
- Place one `"cache_control": {"type": "ephemeral"}` marker on the last Global/Session content block
- Pin high-frequency tools (read_file, write_file, edit_file, bash, grep, glob) at the start of tool definitions with `cache_control`
- Parse `usage.cache_creation_input_tokens` and `cache_read_input_tokens` from `message_start`/`message_delta` events
- Feed values into CacheStats

#### B4. OpenAI Provider Implementation

In `build_messages()`:
- Split system messages into two: static system (Global+Session) and dynamic system (None)
- Add `"stream_options": {"include_usage": true}` for token usage in streamed responses
- Parse `usage.prompt_tokens_details.cached_tokens` from stream chunks
- Feed values into CacheStats

#### B5. CacheStats Update

Add `record_cache_hit()` / `record_cache_miss()` methods on `CacheStats`. AgentLoop calls these after each turn. Console output format:

```
Tokens: 1240 in (580 cached) + 340 out
```

#### B6. Files Changed

| File | Change |
|------|--------|
| `libs/context/include/merak/cache_aware_context.hpp` | Add CacheScope, ScopedBlock; refactor split() |
| `libs/context/src/cache_aware_context.cpp` | Implement scope classification |
| `libs/context/src/context_assembler.cpp` | Return cached/dynamic split |
| `libs/llm/src/anthropic_provider.cpp` | cache_control markers, usage parsing |
| `libs/llm/src/openai_provider.cpp` | Dual system messages, stream_options, cached_tokens |
| `libs/llm/include/merak/llm_provider.hpp` | CacheStats update interface |
| `libs/loop/src/agent_loop.cpp` | Thread cache info through to provider; console output |

---

## Scope Boundaries

**In scope:**
- ErrorKind classification + retry loop for both providers
- Secrets redaction in error messages
- CacheScope model + Anthropic cache_control / OpenAI prefix split
- CacheStats tracking and console display

**Out of scope (deferred):**
- Per-model RateLimitCooldown circuit breaker with fallback chain (astra pattern — larger effort)
- Compaction circuit breaker
- Tool schema tier-pruning
- Cache diagnostics/break detection
- Non-stream fallback on streaming stall
