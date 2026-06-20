#include <merak/openai_provider.hpp>
#include <cassert>
#include <iostream>

using namespace merak;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    tests_run++; \
    std::cout << "  " << name << " ... "

#define PASS() \
    tests_passed++; \
    std::cout << "PASS" << std::endl

void test_build_messages_basic() {
    TEST("build_messages with simple messages");
    LLMConfig cfg;
    cfg.api_key = "sk-test";
    cfg.api_base_url = "https://api.openai.com/v1";
    cfg.default_model = "gpt-4o";
    OpenAIProvider provider(cfg);

    std::vector<Message> msgs = {
        {"user", "hello", {}, {}, ""},
        {"assistant", "hi there", {}, {}, ""},
    };
    auto arr = provider.build_messages(msgs);
    assert(arr.is_array() && arr.size() == 2);
    assert(arr[0]["role"] == "user" && arr[0]["content"] == "hello");
    assert(arr[1]["role"] == "assistant" && arr[1]["content"] == "hi there");
    PASS();
}

void test_build_messages_with_tool_calls() {
    TEST("build_messages with tool_calls");
    LLMConfig cfg;
    cfg.api_key = "sk-test";
    cfg.api_base_url = "https://api.openai.com/v1";
    OpenAIProvider provider(cfg);

    std::vector<Message> msgs = {
        {"user", "read file", {}, {}, ""},
    };
    Message assistant;
    assistant.role = "assistant";
    assistant.tool_calls = {{"call_1", "read_file", R"({"path":"/x"})"}};
    msgs.push_back(assistant);

    Message tool;
    tool.role = "tool";
    tool.content = "file contents";
    tool.tool_call_id = "call_1";
    msgs.push_back(tool);

    auto arr = provider.build_messages(msgs);
    assert(arr.is_array() && arr.size() == 3);
    assert(arr[1].contains("tool_calls") && arr[1]["tool_calls"].is_array());
    assert(arr[1]["tool_calls"][0]["id"] == "call_1");
    assert(arr[2]["tool_call_id"] == "call_1");
    assert(arr[2]["content"] == "file contents");
    PASS();
}

void test_build_tools_empty() {
    TEST("build_tools with empty list");
    LLMConfig cfg;
    cfg.api_key = "sk-test";
    cfg.api_base_url = "https://api.openai.com/v1";
    OpenAIProvider provider(cfg);

    auto arr = provider.build_tools({});
    assert(arr.is_array() && arr.empty());
    PASS();
}

void test_build_tools_with_specs() {
    TEST("build_tools with named tools");
    LLMConfig cfg;
    cfg.api_key = "sk-test";
    cfg.api_base_url = "https://api.openai.com/v1";
    OpenAIProvider provider(cfg);

    ToolSpec t1, t2;
    t1.name = "read_file";
    t1.description = "Read a file";
    t2.name = "write_file";
    t2.description = "Write a file";
    t2.parameters_json = R"({"type":"object"})";

    auto arr = provider.build_tools({t1, t2});
    assert(arr.is_array() && arr.size() == 2);
    assert(arr[0]["type"] == "function");
    assert(arr[0]["function"]["name"] == "read_file");
    assert(arr[1]["function"]["parameters"]["type"] == "object");
    PASS();
}

int main() {
    std::cout << "\nOpenAI Provider Tests\n====================\n";
    test_build_messages_basic();
    test_build_messages_with_tool_calls();
    test_build_tools_empty();
    test_build_tools_with_specs();
    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
