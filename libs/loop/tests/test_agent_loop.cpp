#include <merak/agent_loop.hpp>
#include <merak/sub_agent_runner.hpp>
#include <merak/token_counter.hpp>
#include <merak/compactor.hpp>
#include <merak/memory_store.hpp>
#include <cassert>
#include <iostream>
#include <memory>

using namespace merak;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    tests_run++; \
    std::cout << "  " << name << " ... "
#define PASS() \
    tests_passed++; \
    std::cout << "PASS" << std::endl

// ——— Stubs ————————————————————————————————————————————————

// Minimal stub provider that returns a fixed response.
// Chat is never called by Config-only tests, but the shared_ptr
// must be valid for construction.
class StubProvider : public LlmProvider {
public:
    AgentResponse canned;
    std::string name() const override { return "stub"; }
    std::future<AgentResponse> chat(
        const ChatRequest&,
        std::function<void(StreamChunk)> on_chunk,
        std::shared_ptr<CancellationToken>) override
    {
        // Simulate streaming so response.text is populated via callback.
        if (on_chunk) {
            StreamChunk chunk;
            chunk.text = canned.text;
            on_chunk(chunk);
        }
        std::promise<AgentResponse> p;
        auto future = p.get_future();
        p.set_value(canned);
        return future;
    }
};

// Minimal stub embedding provider so MemoryStore can be constructed
// without requiring a real embedding service.
class StubEmbeddingProvider : public EmbeddingProvider {
public:
    std::future<std::vector<float>> embed(const std::string&) override {
        std::promise<std::vector<float>> p;
        p.set_value({0.0f});
        return p.get_future();
    }
    int dimension() const override { return 1; }
    std::future<std::vector<std::vector<float>>> embed_batch(
        const std::vector<std::string>&) override
    {
        std::promise<std::vector<std::vector<float>>> p;
        p.set_value({{0.0f}});
        return p.get_future();
    }
};

// Helper: build a valid AgentLoop with stub dependencies.
// Only the dependencies that are actually accessed during
// construction / state queries need to be valid.
static std::unique_ptr<AgentLoop> make_test_loop(
    AgentLoop::Config cfg = AgentLoop::Config{})
{
    auto llm = std::make_shared<StubProvider>();
    llm->canned.text = "ok";
    auto tools = std::make_shared<ToolRegistry>();
    auto mem = std::make_shared<MemoryStore>(
        MemoryConfig{},
        std::make_shared<StubEmbeddingProvider>());
    auto counter = std::make_shared<TokenCounter>("gpt-4o");
    auto comp = std::make_shared<Compactor>(llm, counter, "gpt-4o");
    return std::make_unique<AgentLoop>(
        cfg, llm, tools, mem, comp, nullptr, nullptr);
}

// ——— Tests ——————————————————————————————————————————————————

void test_max_turns_config_default() {
    TEST("Config max_turns defaults to 25");
    AgentLoop::Config cfg;
    assert(cfg.max_turns == 25);
    PASS();
}

void test_enable_cache_default() {
    TEST("Config enable_cache defaults to true");
    AgentLoop::Config cfg;
    assert(cfg.enable_cache == true);
    PASS();
}

void test_enable_compaction_default() {
    TEST("Config enable_compaction defaults to true");
    AgentLoop::Config cfg;
    assert(cfg.enable_compaction == true);
    PASS();
}

void test_default_model_default() {
    TEST("Config default_model defaults to gpt-4o");
    AgentLoop::Config cfg;
    assert(cfg.default_model == "gpt-4o");
    PASS();
}

void test_max_output_tokens_default() {
    TEST("Config max_output_tokens defaults to 4096");
    AgentLoop::Config cfg;
    assert(cfg.max_output_tokens == 4096);
    PASS();
}

void test_max_retries_default() {
    TEST("Config max_retries defaults to 3");
    AgentLoop::Config cfg;
    assert(cfg.max_retries == 3);
    PASS();
}

void test_model_max_tokens_default() {
    TEST("Config model_max_tokens defaults to 128000");
    AgentLoop::Config cfg;
    assert(cfg.model_max_tokens == 128000);
    PASS();
}

void test_initial_state_is_idle() {
    TEST("initial state is Idle after construction");
    auto loop = make_test_loop();
    assert(loop->current_state() == TurnState::Idle);
    PASS();
}

void test_session_history_initially_empty() {
    TEST("session_history is empty after construction");
    auto loop = make_test_loop();
    assert(loop->session_history().empty());
    PASS();
}

void test_set_system_prompt_does_not_affect_session_history() {
    TEST("set_system_prompt uses separate storage from session_history");
    AgentLoop::Config cfg;
    cfg.system_prompt = "original";
    auto loop = make_test_loop(cfg);
    loop->set_system_prompt("updated prompt");
    // system_prompt is separate from session_history
    assert(loop->session_history().empty());
    PASS();
}

void test_restore_history_replaces_session() {
    TEST("restore_history replaces existing session history");
    auto loop = make_test_loop();
    std::vector<Message> history = {
        {"user", "hello", {}, {}, ""},
        {"assistant", "hi there", {}, {}, ""},
    };
    loop->restore_history(history);
    assert(loop->session_history().size() == 2);
    assert(loop->session_history()[0].role == "user");
    assert(loop->session_history()[0].content == "hello");
    assert(loop->session_history()[1].role == "assistant");
    assert(loop->session_history()[1].content == "hi there");
    PASS();
}

void test_restore_history_overwrites_previous() {
    TEST("restore_history overwrites previously restored history");
    auto loop = make_test_loop();
    std::vector<Message> old_history = {
        {"user", "old", {}, {}, ""},
    };
    std::vector<Message> new_history = {
        {"user", "new", {}, {}, ""},
        {"assistant", "reply", {}, {}, ""},
    };
    loop->restore_history(old_history);
    assert(loop->session_history().size() == 1);
    loop->restore_history(new_history);
    assert(loop->session_history().size() == 2);
    assert(loop->session_history()[0].content == "new");
    PASS();
}

void test_tools_returns_registry() {
    TEST("tools() returns the tool registry passed at construction");
    auto llm = std::make_shared<StubProvider>();
    llm->canned.text = "ok";
    auto tools = std::make_shared<ToolRegistry>();
    auto mem = std::make_shared<MemoryStore>(
        MemoryConfig{},
        std::make_shared<StubEmbeddingProvider>());
    auto counter = std::make_shared<TokenCounter>("gpt-4o");
    auto comp = std::make_shared<Compactor>(llm, counter, "gpt-4o");
    AgentLoop loop(AgentLoop::Config{}, llm, tools, mem, comp,
                   nullptr, nullptr);
    assert(loop.tools() == tools);
    PASS();
}

void test_pipeline_accessible() {
    TEST("pipeline() returns a valid ContextPipeline reference");
    auto loop = make_test_loop();
    // Just verify the pipeline is accessible, not null
    (void)loop->pipeline();
    PASS();
}

int main() {
    std::cout << "\nAgentLoop Tests\n===============\n";
    test_max_turns_config_default();
    test_enable_cache_default();
    test_enable_compaction_default();
    test_default_model_default();
    test_max_output_tokens_default();
    test_max_retries_default();
    test_model_max_tokens_default();
    test_initial_state_is_idle();
    test_session_history_initially_empty();
    test_set_system_prompt_does_not_affect_session_history();
    test_restore_history_replaces_session();
    test_restore_history_overwrites_previous();
    test_tools_returns_registry();
    test_pipeline_accessible();
    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
