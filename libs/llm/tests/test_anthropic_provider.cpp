#include <merak/anthropic_provider.hpp>
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>

using namespace merak;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) tests_run++; std::cout << "  " << name << " ... "
#define PASS() tests_passed++; std::cout << "PASS" << std::endl

void test_build_body_system_prompt_format() {
    TEST("system prompt as Anthropic array-of-blocks");
    LLMConfig cfg;
    cfg.api_key = "sk-ant-test";
    cfg.api_base_url = "https://api.anthropic.com/v1";
    cfg.default_model = "claude-sonnet-4-20250514";
    AnthropicProvider provider(cfg);

    ChatRequest req;
    req.model = "claude-sonnet-4-20250514";
    req.max_output_tokens = 1024;
    req.messages = {{"system", "You are helpful.", {}, {}, ""},
                    {"user", "hello", {}, {}, ""}};
    req.enable_cache = true;

    auto body = provider.build_request_body(req);
    assert(body.contains("system") && body["system"].is_array());
    auto& sys = body["system"];
    assert(sys.size() >= 1);
    assert(sys[0]["type"] == "text");
    assert(sys[0]["text"] == "You are helpful.");

    // cache_control should be injected
    assert(sys[0].contains("cache_control"));
    assert(sys[0]["cache_control"]["type"] == "ephemeral");
    (void)sys;
    PASS();
}

void test_build_body_tool_use_format() {
    TEST("tool_use content blocks in assistant messages");
    LLMConfig cfg;
    cfg.api_key = "sk-ant-test";
    cfg.api_base_url = "https://api.anthropic.com/v1";
    cfg.default_model = "claude-sonnet-4-20250514";
    AnthropicProvider provider(cfg);

    ChatRequest req;
    req.model = "claude-sonnet-4-20250514";
    req.max_output_tokens = 1024;
    Message assistant;
    assistant.role = "assistant";
    assistant.content = "Let me read that file.";
    assistant.tool_calls = {{"call_1", "read_file", R"({"path":"/x.txt"})"}};
    req.messages = {{"user", "read /x.txt", {}, {}, ""}, assistant};
    req.enable_cache = false;

    auto body = provider.build_request_body(req);
    auto& msgs = body["messages"];
    assert(msgs.is_array() && msgs.size() == 2);

    auto& asst_content = msgs[1]["content"];
    assert(asst_content.is_array());

    // Find the tool_use block
    bool found_tool_use = false;
    for (auto& block : asst_content) {
        if (block["type"] == "tool_use") {
            found_tool_use = true;
            assert(block["name"] == "read_file");
            break;
        }
    }
    assert(found_tool_use);
    (void)found_tool_use;
    PASS();
}

void test_build_body_tool_definitions_with_cache() {
    TEST("cache_control on last tool when enable_cache=true");
    LLMConfig cfg;
    cfg.api_key = "sk-ant-test";
    cfg.api_base_url = "https://api.anthropic.com/v1";
    cfg.default_model = "claude-sonnet-4-20250514";
    AnthropicProvider provider(cfg);

    ChatRequest req;
    req.model = "claude-sonnet-4-20250514";
    req.max_output_tokens = 1024;
    req.messages = {{"user", "hi", {}, {}, ""}};
    req.enable_cache = true;
    req.tools = {
        {"read_file", "Read a file", "", {}},
        {"write_file", "Write a file", "", {}},
    };

    auto body = provider.build_request_body(req);
    assert(body.contains("tools") && body["tools"].is_array());
    auto& last_tool = body["tools"].back();
    assert(last_tool.contains("cache_control"));
    assert(last_tool["cache_control"]["type"] == "ephemeral");
    (void)last_tool;
    PASS();
}

void test_cache_control_absent_when_disabled() {
    TEST("no cache_control when enable_cache=false");
    LLMConfig cfg;
    cfg.api_key = "sk-ant-test";
    cfg.api_base_url = "https://api.anthropic.com/v1";
    cfg.default_model = "claude-sonnet-4-20250514";
    AnthropicProvider provider(cfg);

    ChatRequest req;
    req.model = "claude-sonnet-4-20250514";
    req.max_output_tokens = 1024;
    req.messages = {{"system", "You are helpful.", {}, {}, ""},
                    {"user", "hi", {}, {}, ""}};
    req.enable_cache = false;
    req.tools = {{"read_file", "Read a file", "", {}}};

    auto body = provider.build_request_body(req);
    assert(!body["system"][0].contains("cache_control"));
    assert(!body["tools"].back().contains("cache_control"));
    PASS();
}

void test_thinking_config_disabled() {
    TEST("thinking config not injected when disabled");
    LLMConfig cfg;
    cfg.api_key = "sk-ant-test";
    cfg.api_base_url = "https://api.anthropic.com/v1";
    cfg.default_model = "claude-sonnet-4-20250514";
    AnthropicProvider provider(cfg);

    ChatRequest req;
    req.model = "claude-sonnet-4-20250514";
    req.max_output_tokens = 1024;
    req.messages = {{"user", "hi", {}, {}, ""}};
    req.enable_thinking = false;

    auto body = provider.build_request_body(req);
    assert(!body.contains("thinking"));
    PASS();
}

int main() {
    std::cout << "\nAnthropic Provider Tests\n========================\n";
    test_build_body_system_prompt_format();
    test_build_body_tool_use_format();
    test_build_body_tool_definitions_with_cache();
    test_cache_control_absent_when_disabled();
    test_thinking_config_disabled();
    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
