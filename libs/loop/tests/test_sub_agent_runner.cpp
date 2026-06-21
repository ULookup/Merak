#include <merak/sub_agent_runner.hpp>
#include <merak/agent_loop.hpp>
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

// Minimal stub provider: simulates streaming via the on_chunk
// callback and returns a fixed AgentResponse.
class StubProvider : public LlmProvider {
public:
    AgentResponse canned;
    std::string name() const override { return "stub"; }
    std::future<AgentResponse> chat(
        const ChatRequest&,
        std::function<void(StreamChunk)> on_chunk,
        std::shared_ptr<CancellationToken>) override
    {
        // Simulate streaming so the AgentLoop run_loop callback
        // populates response.text with the canned text.
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

// Minimal embedding provider stub so MemoryStore can be
// constructed without a real embedding service.
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

// Helper: build a valid SubAgentRunner with stub dependencies.
static std::unique_ptr<SubAgentRunner> make_test_runner()
{
    auto llm = std::make_shared<StubProvider>();
    llm->canned.text = "ok";
    auto mem = std::make_shared<MemoryStore>(
        MemoryConfig{},
        std::make_shared<StubEmbeddingProvider>());
    auto tools = std::make_shared<ToolRegistry>();
    return std::make_unique<SubAgentRunner>(
        llm, mem, tools, nullptr, nullptr);
}

// ——— Tests ——————————————————————————————————————————————————

void test_has_agent_returns_false_initially() {
    TEST("has_agent returns false before any registration");
    auto runner = make_test_runner();
    assert(!runner->has_agent("anything"));
    PASS();
}

void test_has_agent_returns_true_after_register() {
    TEST("has_agent returns true after register_profile");
    auto runner = make_test_runner();
    SubAgentConfig cfg;
    cfg.id = "test_agent";
    cfg.system_prompt = "You are a test agent.";
    runner->register_profile(cfg);
    assert(runner->has_agent("test_agent"));
    assert(!runner->has_agent("nonexistent"));
    PASS();
}

void test_delegate_unknown_agent_returns_error() {
    TEST("delegate to unknown agent returns error message");
    auto runner = make_test_runner();
    auto resp = runner->delegate("nonexistent", "do something").get();
    assert(!resp.text.empty());
    // The error message contains the agent id
    assert(resp.text.find("nonexistent") != std::string::npos);
    PASS();
}

void test_sequential_preserves_result_order() {
    TEST("sequential preserves order of agent results");
    auto runner = make_test_runner();

    // Register three agents
    SubAgentConfig cfg;
    cfg.id = "agent_a"; cfg.system_prompt = "test"; runner->register_profile(cfg);
    cfg.id = "agent_b"; runner->register_profile(cfg);
    cfg.id = "agent_c"; runner->register_profile(cfg);

    std::vector<Delegation> pipeline = {
        {"agent_a", "task a"},
        {"agent_b", "task b"},
        {"agent_c", "task c"},
    };

    auto resp = runner->sequential(pipeline).get();

    // Each agent's result appears as "[agent_id]: ..." in the output.
    auto pos_a = resp.text.find("[agent_a]");
    auto pos_b = resp.text.find("[agent_b]");
    auto pos_c = resp.text.find("[agent_c]");
    assert(pos_a != std::string::npos);
    assert(pos_b != std::string::npos);
    assert(pos_c != std::string::npos);
    assert(pos_a < pos_b && pos_b < pos_c);
    PASS();
}

void test_fan_out_returns_map_with_keys() {
    TEST("fan_out returns a map keyed by agent id");
    auto runner = make_test_runner();

    SubAgentConfig cfg;
    cfg.id = "fan_a"; cfg.system_prompt = "test"; runner->register_profile(cfg);
    cfg.id = "fan_b"; runner->register_profile(cfg);

    std::vector<Delegation> tasks = {
        {"fan_a", "task a"},
        {"fan_b", "task b"},
    };

    auto results = runner->fan_out(tasks).get();
    assert(results.size() == 2);
    assert(results.count("fan_a") == 1);
    assert(results.count("fan_b") == 1);
    PASS();
}

int main() {
    std::cout << "\nSubAgentRunner Tests\n====================\n";
    test_has_agent_returns_false_initially();
    test_has_agent_returns_true_after_register();
    test_delegate_unknown_agent_returns_error();
    test_sequential_preserves_result_order();
    test_fan_out_returns_map_with_keys();
    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
