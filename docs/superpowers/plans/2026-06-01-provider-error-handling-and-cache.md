# Provider Error Handling & Prompt Caching — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add HTTP error classification with retry loop and secrets redaction to both LLM providers; implement prompt caching with provider-aware strategies and cache statistics tracking.

**Architecture:** Part A introduces a shared `ErrorKind` enum and `classify_llm_error()` used by both providers in a retry loop with exponential backoff. Part B adds a `cache_breakpoint` index to the assembled context, which providers interpret differently: Anthropic uses `cache_control` markers, OpenAI splits into dual system messages.

**Tech Stack:** C++17, nlohmann/json, libcurl, spdlog

---

### Task 1: Create ErrorKind Enum and Classification

**Files:**
- Create: `libs/core/include/merak/error_kind.hpp`

- [ ] **Step 1: Create the header file**

```cpp
#pragma once
#include <string>

namespace merak {

enum class ErrorKind {
    RateLimit,       // 429 / "rate" in body
    Auth,            // 401, 403
    ServerError,     // 5xx, retryable
    ContextWindow,   // context_length_exceeded — do NOT retry
    InvalidRequest,  // 4xx other — do NOT retry
    StreamTimeout,   // idle timeout during streaming
    Network,         // connection/DNS/transport
    Unknown          // fallback
};

ErrorKind classify_llm_error(long http_status, const std::string& response_body);

bool is_retryable(ErrorKind kind);

int retry_delay_ms(int attempt);

constexpr long kMaxRetryBudgetMs = 30000;

std::string redact_secrets(const std::string& text);

} // namespace merak
```

- [ ] **Step 2: Create the implementation file**

Create `libs/core/src/error_kind.cpp`:

```cpp
#include <merak/error_kind.hpp>
#include <algorithm>
#include <regex>
#include <cmath>

namespace merak {

ErrorKind classify_llm_error(long http_status, const std::string& response_body) {
    std::string lower = response_body;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (http_status == 401 || http_status == 403) return ErrorKind::Auth;

    if (http_status == 429 || lower.find("rate") != std::string::npos) {
        return ErrorKind::RateLimit;
    }

    if (lower.find("context_length_exceeded") != std::string::npos ||
        lower.find("maximum context length") != std::string::npos ||
        lower.find("too many tokens") != std::string::npos) {
        return ErrorKind::ContextWindow;
    }

    if (http_status >= 500) return ErrorKind::ServerError;
    if (http_status >= 400) return ErrorKind::InvalidRequest;

    return ErrorKind::Unknown;
}

bool is_retryable(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::RateLimit:
        case ErrorKind::ServerError:
        case ErrorKind::Network:
            return true;
        default:
            return false;
    }
}

int retry_delay_ms(int attempt) {
    int base = 1000;
    int delay = base * (1 << (attempt - 1));
    return std::min(delay, 30000);
}

std::string redact_secrets(const std::string& text) {
    std::string result = text;
    result = std::regex_replace(result, std::regex("sk-[a-zA-Z0-9]+"), "[REDACTED]");
    result = std::regex_replace(result, std::regex("Bearer [a-zA-Z0-9_\\-]+"), "Bearer [REDACTED]");
    result = std::regex_replace(result, std::regex("key-[a-zA-Z0-9]+"), "[REDACTED]");
    return result;
}

} // namespace merak
```

- [ ] **Step 3: Add error_kind.cpp to CMakeLists.txt**

Read `libs/core/CMakeLists.txt`, add `src/error_kind.cpp` to the source file list.

- [ ] **Step 4: Build to verify compilation**

```bash
cmake --build /home/icepop/Merak/build 2>&1 | tail -5
```

Expected: `[100%] Built target merak-core`

- [ ] **Step 5: Commit**

```bash
git add libs/core/include/merak/error_kind.hpp libs/core/src/error_kind.cpp libs/core/CMakeLists.txt
git commit -m "feat: add ErrorKind enum, classification, and secrets redaction"
```

---

### Task 2: Add Retry Loop to OpenAIProvider

**Files:**
- Modify: `libs/llm/src/openai_provider.cpp`

- [ ] **Step 1: Add include at top of file**

After line 3 (`#include <spdlog/spdlog.h>`), add:

```cpp
#include <merak/error_kind.hpp>
#include <thread>
#include <chrono>
```

- [ ] **Step 2: Replace the single curl call with retry loop**

Replace lines 124-129 (from `CURLcode res = curl_easy_perform(curl);` to the throw):

```cpp
        AgentResponse response;
        auto start_time = std::chrono::steady_clock::now();

        for (int attempt = 0; attempt <= config_.max_retries; attempt++) {
            // Reset accumulators for each attempt
            response_text.clear();
            input_tokens = 0;
            output_tokens = 0;
            accumulated_tool_calls_json = nlohmann::json::array();

            CURL* curl = curl_easy_init();
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers,
                ("Authorization: Bearer " + config_.api_key).c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

            // Re-attach write callback with fresh capture
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                    auto* cb = static_cast<decltype(&write_callback)>(userdata);
                    std::string data(ptr, size * nmemb);
                    (*cb)(data);
                    return size * nmemb;
                });
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_callback);

            CURLcode res = curl_easy_perform(curl);

            long http_status = 0;
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
            }

            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);

            // Check curl-level errors
            if (res != CURLE_OK) {
                ErrorKind kind = (res == CURLE_OPERATION_TIMEDOUT)
                    ? ErrorKind::StreamTimeout : ErrorKind::Network;
                spdlog::warn("OpenAI curl error (attempt {}/{}): {}",
                    attempt + 1, config_.max_retries + 1, curl_easy_strerror(res));

                if (!is_retryable(kind) || attempt >= config_.max_retries) {
                    throw AgentError(ErrorType::LLM_ERROR, curl_easy_strerror(res));
                }
                int delay = retry_delay_ms(attempt + 1);
                spdlog::info("OpenAI retrying in {}ms...", delay);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                continue;
            }

            // Check HTTP-level errors
            if (http_status >= 400) {
                ErrorKind kind = classify_llm_error(http_status, response_text);
                spdlog::warn("OpenAI HTTP {} (attempt {}/{}), kind={}",
                    http_status, attempt + 1, config_.max_retries + 1, (int)kind);

                if (kind == ErrorKind::Auth) {
                    throw AgentError(ErrorType::LLM_ERROR,
                        "OpenAI auth failed: " + redact_secrets(response_text.substr(0, 200)));
                }
                if (!is_retryable(kind) || attempt >= config_.max_retries) {
                    throw AgentError(ErrorType::LLM_ERROR,
                        "OpenAI request failed (HTTP " + std::to_string(http_status) + ")");
                }

                int delay = retry_delay_ms(attempt + 1);
                if (kind == ErrorKind::RateLimit) {
                    delay = std::max(delay, 5000);
                }
                spdlog::info("OpenAI retrying in {}ms...", delay);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                continue;
            }

            // Check total budget
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed > kMaxRetryBudgetMs) {
                spdlog::error("OpenAI retry budget exhausted after {}ms", elapsed);
                throw AgentError(ErrorType::LLM_ERROR, "Retry budget exhausted");
            }

            // Success — break out of retry loop
            break;
        }

        // Build response from accumulated data (existing code)
        response.tool_calls = ...; // existing lines 136-148 moved here
```

Wait — this approach has a problem. The original code not at lines 124-149 needs to be restructured. The write_callback, URL, and body_str are defined BEFORE the retry loop. The retry loop should only wrap the curl_easy_perform and response checking.

Let me rewrite this more carefully:

Replace lines 33-149 (from `std::string url = ...` through `return response;`) in the chat() function:

```cpp
        std::string url = config_.api_base_url + "/chat/completions";
        std::string body_str = body.dump();
        spdlog::debug("OpenAI request: url={}, body_size={}", url, body_str.size());

        std::string response_text;
        int input_tokens = 0, output_tokens = 0;
        nlohmann::json accumulated_tool_calls_json = nlohmann::json::array();

        auto write_callback = [&](const std::string& data) {
            std::istringstream stream(data);
            std::string line;
            while (std::getline(stream, line)) {
                if (line.rfind("data: ", 0) != 0) continue;
                std::string json_line = line.substr(6);
                if (json_line == "[DONE]") {
                    StreamChunk chunk{"", true, false};
                    on_chunk(chunk);
                    continue;
                }

                try {
                    auto j = nlohmann::json::parse(json_line);
                    auto& choices = j["choices"];
                    if (!choices.empty()) {
                        auto& delta = choices[0]["delta"];

                        if (delta.contains("content") && !delta["content"].is_null()) {
                            std::string text = delta["content"].get<std::string>();
                            response_text += text;
                            StreamChunk chunk{text, false, false};
                            on_chunk(chunk);
                        }

                        if (delta.contains("tool_calls")) {
                            for (auto& tc : delta["tool_calls"]) {
                                int idx = tc.value("index", 0);
                                while (idx >= (int)accumulated_tool_calls_json.size()) {
                                    accumulated_tool_calls_json.push_back(
                                        nlohmann::json::object());
                                }
                                auto& acc = accumulated_tool_calls_json[idx];
                                if (tc.contains("id") && !tc["id"].is_null()) {
                                    acc["id"] = tc["id"];
                                }
                                if (tc.contains("function")) {
                                    auto& func = tc["function"];
                                    if (func.contains("name") && !func["name"].is_null()) {
                                        acc["function"]["name"] = func["name"];
                                    }
                                    if (func.contains("arguments")) {
                                        std::string prev = acc["function"].value("arguments", "");
                                        acc["function"]["arguments"] = prev +
                                            func["arguments"].get<std::string>();
                                    }
                                }
                            }
                        }
                    }

                    if (j.contains("usage")) {
                        input_tokens = j["usage"].value("prompt_tokens", 0);
                        output_tokens = j["usage"].value("completion_tokens", 0);
                    }
                } catch (const nlohmann::json::exception& e) {
                    spdlog::warn("SSE parse error: {}", e.what());
                }
            }
        };

        auto start_time = std::chrono::steady_clock::now();

        for (int attempt = 0; attempt <= config_.max_retries; attempt++) {
            response_text.clear();
            input_tokens = 0; output_tokens = 0;
            accumulated_tool_calls_json = nlohmann::json::array();

            CURL* curl = curl_easy_init();
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers,
                ("Authorization: Bearer " + config_.api_key).c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                    auto* cb = static_cast<decltype(&write_callback)>(userdata);
                    std::string data(ptr, size * nmemb);
                    (*cb)(data);
                    return size * nmemb;
                });
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_callback);

            CURLcode res = curl_easy_perform(curl);

            long http_status = 0;
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
            }

            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);

            if (res != CURLE_OK) {
                ErrorKind kind = (res == CURLE_OPERATION_TIMEDOUT)
                    ? ErrorKind::StreamTimeout : ErrorKind::Network;
                spdlog::warn("OpenAI curl error (attempt {}/{}): {}",
                    attempt + 1, config_.max_retries + 1, curl_easy_strerror(res));
                if (!is_retryable(kind) || attempt >= config_.max_retries) {
                    throw AgentError(ErrorType::LLM_ERROR, curl_easy_strerror(res));
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(retry_delay_ms(attempt + 1)));
                continue;
            }

            if (http_status >= 400) {
                ErrorKind kind = classify_llm_error(http_status, response_text);
                spdlog::warn("OpenAI HTTP {} (attempt {}/{}), kind={}",
                    http_status, attempt + 1, config_.max_retries + 1, (int)kind);

                if (kind == ErrorKind::Auth) {
                    throw AgentError(ErrorType::LLM_ERROR,
                        "OpenAI auth failed: " + redact_secrets(response_text.substr(0, 200)));
                }
                if (!is_retryable(kind) || attempt >= config_.max_retries) {
                    throw AgentError(ErrorType::LLM_ERROR,
                        "OpenAI request failed (HTTP " + std::to_string(http_status) + ")");
                }

                int delay = retry_delay_ms(attempt + 1);
                if (kind == ErrorKind::RateLimit) delay = std::max(delay, 5000);
                spdlog::info("OpenAI retrying in {}ms...", delay);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                continue;
            }

            // Budget check
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed > kMaxRetryBudgetMs) {
                throw AgentError(ErrorType::LLM_ERROR, "Retry budget exhausted");
            }

            break;  // Success
        }

        AgentResponse response;
        for (auto& tc_json : accumulated_tool_calls_json) {
            ToolCall tc;
            tc.id = tc_json.value("id", "");
            tc.name = tc_json["function"].value("name", "");
            tc.arguments = tc_json["function"].value("arguments", "");
            response.tool_calls.push_back(std::move(tc));
        }
        response.text = response_text;
        response.total_input_tokens = input_tokens;
        response.total_output_tokens = output_tokens;

        stats_.total_requests++;
        return response;
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build /home/icepop/Merak/build 2>&1 | grep -E "error|Error" | head -5
```

Expected: No errors.

- [ ] **Step 3: Commit**

```bash
git add libs/llm/src/openai_provider.cpp
git commit -m "feat: add HTTP error classification and retry loop to OpenAIProvider"
```

---

### Task 3: Add Retry Loop to AnthropicProvider

**Files:**
- Modify: `libs/llm/src/anthropic_provider.cpp`

- [ ] **Step 1: Add includes at top of file**

After line 3 (`#include <spdlog/spdlog.h>`), add:

```cpp
#include <merak/error_kind.hpp>
#include <thread>
#include <chrono>
```

- [ ] **Step 2: Replace curl call with retry loop**

Same pattern as Task 2, applied to AnthropicProvider. Replace lines 226-245 (from `CURLcode res = curl_easy_perform(curl);` through `return response;`):

```cpp
        auto start_time = std::chrono::steady_clock::now();

        for (int attempt = 0; attempt <= config_.max_retries; attempt++) {
            response_text.clear();
            input_tokens = 0; output_tokens = 0;
            pending_tools.clear();
            accumulated_tool_calls.clear();
            current_event.clear();
            current_data.clear();
            line_buffer.clear();

            CURL* curl = curl_easy_init();
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers,
                ("x-api-key: " + config_.api_key).c_str());
            headers = curl_slist_append(headers,
                "anthropic-version: 2023-06-01");
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                    auto* cb = static_cast<decltype(&write_callback)>(userdata);
                    std::string data(ptr, size * nmemb);
                    (*cb)(data);
                    return size * nmemb;
                });
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_callback);

            CURLcode res = curl_easy_perform(curl);

            long http_status = 0;
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
            }

            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);

            if (res != CURLE_OK) {
                ErrorKind kind = (res == CURLE_OPERATION_TIMEDOUT)
                    ? ErrorKind::StreamTimeout : ErrorKind::Network;
                spdlog::warn("Anthropic curl error (attempt {}/{}): {}",
                    attempt + 1, config_.max_retries + 1, curl_easy_strerror(res));
                if (!is_retryable(kind) || attempt >= config_.max_retries) {
                    throw AgentError(ErrorType::LLM_ERROR, curl_easy_strerror(res));
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(retry_delay_ms(attempt + 1)));
                continue;
            }

            if (http_status >= 400) {
                ErrorKind kind = classify_llm_error(http_status, response_text);
                spdlog::warn("Anthropic HTTP {} (attempt {}/{}), kind={}",
                    http_status, attempt + 1, config_.max_retries + 1, (int)kind);

                if (kind == ErrorKind::Auth) {
                    throw AgentError(ErrorType::LLM_ERROR,
                        "Anthropic auth failed: " + redact_secrets(response_text.substr(0, 200)));
                }
                if (!is_retryable(kind) || attempt >= config_.max_retries) {
                    throw AgentError(ErrorType::LLM_ERROR,
                        "Anthropic request failed (HTTP " + std::to_string(http_status) + ")");
                }

                int delay = retry_delay_ms(attempt + 1);
                if (kind == ErrorKind::RateLimit) delay = std::max(delay, 5000);
                spdlog::info("Anthropic retrying in {}ms...", delay);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                continue;
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed > kMaxRetryBudgetMs) {
                throw AgentError(ErrorType::LLM_ERROR, "Retry budget exhausted");
            }

            break;  // Success
        }

        AgentResponse response;
        response.tool_calls = std::move(accumulated_tool_calls);
        response.text = response_text;
        response.total_input_tokens = input_tokens;
        response.total_output_tokens = output_tokens;

        stats_.total_requests++;
        return response;
```

Note: The variable declarations `response_text`, `input_tokens`, `output_tokens`, `pending_tools`, `accumulated_tool_calls`, `current_event`, `current_data`, `line_buffer` that are currently defined between lines 118-127 must be moved BEFORE the retry loop (before the `auto start_time` line) so they persist across retry attempts and are cleared at the start of each attempt.

- [ ] **Step 3: Build and verify**

```bash
cmake --build /home/icepop/Merak/build 2>&1 | grep -E "error" | head -5
```

Expected: No errors.

- [ ] **Step 4: Commit**

```bash
git add libs/llm/src/anthropic_provider.cpp
git commit -m "feat: add HTTP error classification and retry loop to AnthropicProvider"
```

---

### Task 4: Add CacheScope and AssembledContext to Context Pipeline

**Files:**
- Modify: `libs/context/include/merak/cache_aware_context.hpp`
- Modify: `libs/context/include/merak/context_assembler.hpp`
- Modify: `libs/context/src/context_assembler.cpp`
- Modify: `libs/loop/src/agent_loop.cpp`

- [ ] **Step 1: Add CacheScope and AssembledContext to cache_aware_context.hpp**

After the existing `Split` struct, add:

```cpp
enum class CacheScope { Global, Session, None };

struct AssembledContext {
    std::vector<Message> cached_prefix;
    std::vector<Message> dynamic_suffix;
    int cache_breakpoint() const { return (int)cached_prefix.size(); }
};
```

- [ ] **Step 2: Change ContextAssembler::assemble() return type**

In `context_assembler.hpp`, replace the `assemble()` declaration:

```cpp
    AssembledContext assemble(
        const std::string& system_prompt,
        const nlohmann::json& tool_specs_json,
        const std::vector<Message>& history,
        const std::vector<MemorySnippet>& memory_snippets = {}
    );
```

- [ ] **Step 3: Update context_assembler.cpp implementation**

Replace the body of `assemble()` (lines 16-57) — system prompt goes to `cached_prefix`, memory goes to `cached_prefix`, history goes to `dynamic_suffix`:

```cpp
AssembledContext ContextAssembler::assemble(
    const std::string& system_prompt,
    const nlohmann::json& /*tool_specs_json*/,
    const std::vector<Message>& history,
    const std::vector<MemorySnippet>& memory_snippets
) {
    AssembledContext result;

    // System prompt → cached (Global scope)
    result.cached_prefix.push_back(build_system_message(system_prompt));

    int sys_tokens = counter_->count(result.cached_prefix.back());
    spdlog::debug("Context: system_prompt = {} tokens", sys_tokens);

    // Memory snippets → cached (Session scope)
    if (!memory_snippets.empty()) {
        auto mem_msg = build_memory_message(memory_snippets);
        int mem_tokens = counter_->count(mem_msg);
        int max_mem_tokens = (int)(budget_.model_max_tokens * budget_.memory_ratio);

        if (mem_tokens <= max_mem_tokens) {
            result.cached_prefix.push_back(std::move(mem_msg));
            spdlog::debug("Context: memory = {} tokens", mem_tokens);
        } else {
            spdlog::warn("Context: memory too large ({} > {}), truncated",
                mem_tokens, max_mem_tokens);
        }
    }

    // History → dynamic (None scope), budget-limited
    int used = counter_->count(result.cached_prefix);
    int available = effective_budget() - used;
    int keep_count = counter_->fit_in_budget(history, available);

    int start = std::max(0, (int)history.size() - keep_count);
    for (int i = start; i < (int)history.size(); i++) {
        result.dynamic_suffix.push_back(history[i]);
    }

    int total = counter_->count(result.cached_prefix) + counter_->count(result.dynamic_suffix);
    spdlog::info("Context: assembled {} cached + {} dynamic = {} tokens (budget={})",
        result.cached_prefix.size(), result.dynamic_suffix.size(),
        total, effective_budget());

    return result;
}
```

- [ ] **Step 4: Update AgentLoop::build_context() to use AssembledContext**

In `agent_loop.cpp`, replace the body of `build_context()` (lines 148-164):

```cpp
std::vector<Message> AgentLoop::build_context(const std::string& /*user_message*/) {
    auto mem_future = memory_->search(user_message, 5);  // Note: user_message not available here
    // ... needs refactor to pass user_message as parameter
```

Actually, `build_context()` takes `user_message` but doesn't use it. Let me look at the current implementation again:

```cpp
std::vector<Message> AgentLoop::build_context(const std::string& user_message) {
    auto mem_future = memory_->search(user_message, 5);
    std::vector<MemorySnippet> mem_snippets;
    if (mem_future.valid()) {
        auto mem_result = mem_future.get();
        if (mem_result.has_value()) {
            mem_snippets = mem_result.value();
        }
    }

    return context_->assemble(
        config_.system_prompt,
        tools_->all_tools_json(),
        session_history_,
        mem_snippets
    );
}
```

This returns `vector<Message>` but now assemble returns `AssembledContext`. We need to update this AND the callers in `run()`. The run() method needs the AssembledContext for cache info, not just the flat list.

Update `build_context()`:

```cpp
AssembledContext AgentLoop::build_context(const std::string& user_message) {
    auto mem_future = memory_->search(user_message, 5);
    std::vector<MemorySnippet> mem_snippets;
    if (mem_future.valid()) {
        auto mem_result = mem_future.get();
        if (mem_result.has_value()) {
            mem_snippets = mem_result.value();
        }
    }

    return context_->assemble(
        config_.system_prompt,
        tools_->all_tools_json(),
        session_history_,
        mem_snippets
    );
}
```

Update the declaration in `agent_loop.hpp`:
```cpp
    AssembledContext build_context(const std::string& user_message);
```

- [ ] **Step 5: Update run() to use AssembledContext**

In `agent_loop.cpp` run() method (around lines 53-54), change:

```cpp
auto context_messages = build_context(user_message);

auto split = CacheAwareContext::split(context_messages);
spdlog::debug("Loop: turn {} — {}", turn_count, CacheAwareContext::info(split));
```

To:

```cpp
auto assembled = build_context(user_message);

// Flat list for the LLM request
std::vector<Message> context_messages;
context_messages.insert(context_messages.end(),
    assembled.cached_prefix.begin(), assembled.cached_prefix.end());
context_messages.insert(context_messages.end(),
    assembled.dynamic_suffix.begin(), assembled.dynamic_suffix.end());
```

Then in the ChatRequest construction (around line 62-65), add:

```cpp
ChatRequest req;
req.model = config_.default_model;
req.messages = context_messages;
req.enable_cache = config_.enable_cache;
req.cache_breakpoint = assembled.cache_breakpoint();  // NEW
```

- [ ] **Step 6: Add cache_breakpoint field to ChatRequest**

In `libs/llm/include/merak/llm_provider.hpp`, add to `ChatRequest`:

```cpp
struct ChatRequest {
    std::string model;
    std::vector<Message> messages;
    std::vector<ToolSpec> tools;
    int max_output_tokens = 4096;
    bool enable_cache = true;
    int cache_breakpoint = -1;  // index where dynamic suffix starts, -1 = no caching
};
```

- [ ] **Step 7: Add `#include <merak/cache_aware_context.hpp>` to agent_loop.hpp**

- [ ] **Step 8: Build and verify**

```bash
cmake --build /home/icepop/Merak/build 2>&1 | grep -E "error" | head -10
```

Expected: No errors.

- [ ] **Step 9: Commit**

```bash
git add libs/context/include/merak/cache_aware_context.hpp \
        libs/context/include/merak/context_assembler.hpp \
        libs/context/src/context_assembler.cpp \
        libs/loop/include/merak/agent_loop.hpp \
        libs/loop/src/agent_loop.cpp \
        libs/llm/include/merak/llm_provider.hpp
git commit -m "feat: add AssembledContext with cache breakpoint for cached/dynamic split"
```

---

### Task 5: Implement Anthropic cache_control Markers

**Files:**
- Modify: `libs/llm/src/anthropic_provider.cpp`

- [ ] **Step 1: Update build_request_body() for system prompt caching**

Currently `body["system"] = m.content;` sets a plain string. Change to an array with cache_control on the last text block:

Replace the system prompt handling in `build_request_body()` (around lines 25-28):

```cpp
        if (m.role == "system") {
            // Convert system prompt to content-block array for cache_control support
            nlohmann::json system_blocks = nlohmann::json::array();
            nlohmann::json text_block;
            text_block["type"] = "text";
            text_block["text"] = m.content;
            if (request.enable_cache) {
                text_block["cache_control"] = {{"type", "ephemeral"}};
            }
            system_blocks.push_back(text_block);
            body["system"] = system_blocks;
            continue;
        }
```

- [ ] **Step 2: Add cache_control to pinned tools**

After the tools loop (after `body["tools"] = tools_arr;` around line 88), add cache_control to the last pinned tool:

```cpp
    body["tools"] = tools_arr;

    // Pin high-frequency tools for caching
    if (request.enable_cache && !tools_arr.empty()) {
        static const std::vector<std::string> kPinnedTools = {
            "read_file", "write_file", "edit_file",
            "execute_bash", "grep", "glob"
        };
        nlohmann::json sorted_tools = nlohmann::json::array();
        nlohmann::json other_tools = nlohmann::json::array();
        for (auto& t : tools_arr) {
            bool pinned = false;
            for (auto& name : kPinnedTools) {
                if (t["name"] == name) { pinned = true; break; }
            }
            if (pinned) sorted_tools.push_back(t);
            else other_tools.push_back(t);
        }
        // Place cache_control on LAST pinned tool
        if (!sorted_tools.empty()) {
            sorted_tools.back()["cache_control"] = {{"type", "ephemeral"}};
        }
        for (auto& t : other_tools) sorted_tools.push_back(t);
        body["tools"] = sorted_tools;
    }
```

- [ ] **Step 3: Parse cache usage from SSE events**

In `process_event()` (around lines 132-191), update `message_start` and `message_delta` handlers to extract cache tokens:

In the `message_start` handler (around line 138):
```cpp
if (event_type == "message_start") {
    if (j.contains("message") && j["message"].contains("usage")) {
        auto& usage = j["message"]["usage"];
        input_tokens = usage.value("input_tokens", 0);
        // Extract cache read tokens
        if (usage.contains("cache_read_input_tokens")) {
            int cached = usage["cache_read_input_tokens"].get<int>();
            stats_.cache_read_tokens += cached;
        }
        if (usage.contains("cache_creation_input_tokens")) {
            int created = usage["cache_creation_input_tokens"].get<int>();
            stats_.cache_creation_tokens += created;
        }
    }
}
```

In the `message_delta` handler (around line 180):
```cpp
else if (event_type == "message_delta") {
    if (j.contains("usage")) {
        output_tokens = j["usage"].value("output_tokens", 0);
        if (j["usage"].contains("cache_read_input_tokens")) {
            int cached = j["usage"]["cache_read_input_tokens"].get<int>();
            stats_.cache_read_tokens += cached;
        }
    }
}
```

- [ ] **Step 4: Add cache tracking fields to CacheStats**

In `libs/llm/include/merak/llm_provider.hpp`, update `CacheStats`:

```cpp
struct CacheStats {
    int total_requests = 0;
    int cache_hits = 0;
    int input_tokens_saved = 0;
    int cache_read_tokens = 0;
    int cache_creation_tokens = 0;
    double hit_rate() const {
        return total_requests > 0 ? (double)cache_hits / total_requests : 0.0;
    }
    bool was_cache_hit() const { return cache_read_tokens > 0; }
};
```

- [ ] **Step 5: Build and verify**

```bash
cmake --build /home/icepop/Merak/build 2>&1 | grep -E "error" | head -5
```

Expected: No errors.

- [ ] **Step 6: Commit**

```bash
git add libs/llm/src/anthropic_provider.cpp libs/llm/include/merak/llm_provider.hpp
git commit -m "feat: implement Anthropic cache_control markers and cache usage parsing"
```

---

### Task 6: Implement OpenAI Dual System Messages and stream_options

**Files:**
- Modify: `libs/llm/src/openai_provider.cpp`

- [ ] **Step 1: Split system messages at cache breakpoint**

In `build_messages()`, change the logic so that when `cache_breakpoint > 0`, system messages before the breakpoint go into a separate first system message, and the rest are dynamic:

```cpp
nlohmann::json OpenAIProvider::build_messages(
    const std::vector<Message>& msgs,
    int cache_breakpoint
) const {
    nlohmann::json arr = nlohmann::json::array();

    for (int i = 0; i < (int)msgs.size(); i++) {
        auto& m = msgs[i];
        nlohmann::json item;
        item["role"] = m.role;
        item["content"] = m.content;

        if (!m.tool_calls.empty()) {
            auto tcs = nlohmann::json::array();
            for (auto& tc : m.tool_calls) {
                nlohmann::json t;
                t["id"] = tc.id;
                t["type"] = "function";
                t["function"]["name"] = tc.name;
                t["function"]["arguments"] = tc.arguments;
                tcs.push_back(t);
            }
            item["tool_calls"] = tcs;
        }

        if (m.tool_call_id.has_value()) {
            item["tool_call_id"] = m.tool_call_id.value();
        }

        arr.push_back(item);
    }

    return arr;
}
```

Wait — the current `build_messages()` doesn't take a `cache_breakpoint` parameter. And it's called from `chat()` which doesn't have access to that info through the current call chain. Let me adjust the approach.

Instead of modifying `build_messages()`, handle the split directly in `chat()` where the `ChatRequest` is available:

In the `chat()` method, after `body["messages"] = build_messages(request.messages);` (around line 25), add:

```cpp
body["messages"] = build_messages(request.messages);

// OpenAI auto-prefix caching: split static system into separate message
if (request.enable_cache && request.cache_breakpoint > 0 &&
    request.cache_breakpoint < (int)request.messages.size()) {
    // The first cache_breakpoint messages form the cached prefix
    // OpenAI auto-detects common prefixes, so we just need consistent ordering
    // No special markers needed — the split itself enables prefix caching
}
```

Actually, for OpenAI the key insight is simpler. The cached messages are already at the front of the messages array. OpenAI's API automatically caches message prefixes that appear in multiple requests. So we just need to ensure the cached_prefix comes first (which it already does), and add `stream_options` for usage tracking. No message splitting needed — that's the beauty of OpenAI's approach.

- [ ] **Step 1: Add stream_options for usage in streaming**

In `chat()`, after `body["stream"] = true;` (around line 26), change to:

```cpp
body["stream"] = true;
body["stream_options"] = {{"include_usage", true}};
```

- [ ] **Step 2: Parse cached_tokens from usage**

The existing usage parsing at lines 104-107 already extracts `prompt_tokens` and `completion_tokens`. Add cache extraction in the same block:

```cpp
if (j.contains("usage")) {
    input_tokens = j["usage"].value("prompt_tokens", 0);
    output_tokens = j["usage"].value("completion_tokens", 0);
    if (j["usage"].contains("prompt_tokens_details")) {
        int cached = j["usage"]["prompt_tokens_details"].value("cached_tokens", 0);
        if (cached > 0) {
            stats_.cache_read_tokens += cached;
            stats_.cache_hits++;
            stats_.input_tokens_saved += cached;
        }
    }
}
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build /home/icepop/Merak/build 2>&1 | grep -E "error" | head -5
```

Expected: No errors.

- [ ] **Step 4: Commit**

```bash
git add libs/llm/src/openai_provider.cpp
git commit -m "feat: add stream_options and cache token parsing to OpenAIProvider"
```

---

### Task 7: Wire CacheStats Display in AgentLoop

**Files:**
- Modify: `libs/loop/src/agent_loop.cpp`
- Modify: `cli/src/main.cpp`

- [ ] **Step 1: Track cache stats in AgentLoop::run()**

After `llm_response` is received (around line 83-85), add cache hit tracking:

```cpp
auto llm_response = llm_future.get();
response.total_input_tokens += llm_response.total_input_tokens;
response.total_output_tokens += llm_response.total_output_tokens;

// Update cache stats (via provider's stats)
// Stats are updated internally by providers during SSE parsing
// We read them back for display
```

Actually, CacheStats is per-provider and already being updated inside the provider. We need access to it from AgentLoop. Currently, `LlmProvider` doesn't expose CacheStats publicly. Both providers have their own `stats_` member.

Add a virtual method to `LlmProvider`:

```cpp
virtual const CacheStats& cache_stats() const {
    static CacheStats empty;
    return empty;
}
```

Both providers override:

```cpp
const CacheStats& cache_stats() const override { return stats_; }
```

- [ ] **Step 2: Update console output in main.cpp**

In `main.cpp`, after line 309 (`std::cout << "Tokens: " << ...`), add cache info:

```cpp
std::cout << "\n---\n";
auto& cs = llm->cache_stats();
std::cout << "Tokens: " << response.total_input_tokens
    << " in";
if (cs.was_cache_hit()) {
    std::cout << " (" << cs.cache_read_tokens << " cached)";
}
std::cout << " + " << response.total_output_tokens << " out";
```

- [ ] **Step 3: Add cache_stats() to LlmProvider**

In `libs/llm/include/merak/llm_provider.hpp`, add to `LlmProvider`:

```cpp
virtual const CacheStats& cache_stats() const {
    static const CacheStats empty;
    return empty;
}
```

In `openai_provider.hpp` and `anthropic_provider.hpp`, add:
```cpp
const CacheStats& cache_stats() const override { return stats_; }
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build /home/icepop/Merak/build 2>&1 | grep -E "error" | head -5
```

Expected: No errors.

- [ ] **Step 5: Commit**

```bash
git add libs/loop/src/agent_loop.cpp \
        cli/src/main.cpp \
        libs/llm/include/merak/llm_provider.hpp \
        libs/llm/include/merak/openai_provider.hpp \
        libs/llm/include/merak/anthropic_provider.hpp
git commit -m "feat: wire cache stats display into AgentLoop output"
```

---

### Task 8: End-to-End Build and Smoke Test

- [ ] **Step 1: Full clean build**

```bash
rm -f /home/icepop/Merak/build/cli/merak-cli
cmake --build /home/icepop/Merak/build 2>&1 | tail -5
```

Expected: `[100%] Built target merak-cli`

- [ ] **Step 2: Install**

```bash
cp /home/icepop/Merak/build/cli/merak-cli /home/icepop/.local/bin/merak
```

- [ ] **Step 3: Run smoke test**

```bash
echo "hello" | timeout 30 merak 2>&1 | head -5
```

Expected: Provider and model info displayed, no crash.

- [ ] **Step 4: Commit any remaining changes**

```bash
git status
```
