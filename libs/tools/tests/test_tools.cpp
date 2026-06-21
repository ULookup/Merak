#include <merak/tool_registry.hpp>
#include <merak/builtin_tools.hpp>
#include <merak/shell_tool.hpp>
#include <merak/mcp_tool_wrapper.hpp>
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

int main() {
    std::cout << "\nTool Tests\n=========\n";

    // Test registration
    TEST("register_tool adds tool to registry");
    ToolRegistry registry;
    registry.register_tool(std::make_unique<tools::ReadFileTool>());
    assert(registry.size() == 1);
    assert(registry.find_spec("read_file").has_value());
    assert(!registry.find_spec("nonexistent").has_value());
    PASS();

    // Test duplicate registration
    TEST("duplicate registration replaces existing tool");
    registry.register_tool(std::make_unique<tools::ReadFileTool>());
    assert(registry.size() == 1);
    PASS();

    // Test spec
    TEST("find_spec returns correct ToolSpec");
    auto spec = registry.find_spec("read_file").value();
    assert(spec.name == "read_file");
    assert(spec.source == "builtin");
    PASS();

    // Test permission check
    TEST("check_permission enforces permission levels");
    assert(registry.check_permission("read_file", "auto"));
    assert(registry.check_permission("read_file", "ask"));
    assert(!registry.check_permission("nonexistent", "auto"));
    PASS();

    // Test all_tools
    TEST("all_tools returns registered tools");
    auto all = registry.all_tools();
    assert(all.size() == 1);
    PASS();

    // Test clone
    TEST("clone creates identical tool");
    auto* tool = registry.get_tool("read_file");
    assert(tool != nullptr);
    auto cloned = tool->clone();
    assert(cloned->spec().name == "read_file");
    PASS();

    // —— JSON Schema validation tests ——

    TEST("validation rejects missing required field");
    {
        ToolRegistry reg2;
        reg2.register_tool(std::make_unique<tools::BashTool>());
        ToolCall call;
        call.name = "execute_bash";
        call.id = "call_1";
        call.arguments = "{}";  // missing required "command"
        auto result = reg2.execute(call, {}).get();
        assert(result.is_error);
        assert(result.output.find("missing required field") != std::string::npos);
    }
    PASS();

    TEST("validation rejects wrong type for field");
    {
        ToolRegistry reg3;
        reg3.register_tool(std::make_unique<tools::BashTool>());
        ToolCall call;
        call.name = "execute_bash";
        call.id = "call_2";
        call.arguments = R"({"command": 123})";  // command should be string
        auto result = reg3.execute(call, {}).get();
        assert(result.is_error);
        assert(result.output.find("expected type") != std::string::npos);
    }
    PASS();

    TEST("validation passes for valid arguments");
    {
        ToolRegistry reg4;
        reg4.register_tool(std::make_unique<tools::BashTool>());
        ToolCall call;
        call.name = "execute_bash";
        call.id = "call_3";
        call.arguments = R"({"command": "echo hello"})";  // valid
        auto result = reg4.execute(call, {}).get();
        // Should NOT be a validation error (may fail at execution but not validation)
        assert(result.output.find("missing required field") == std::string::npos);
        assert(result.output.find("expected type") == std::string::npos);
    }
    PASS();

    TEST("validation passes for tool without schema");
    {
        ToolRegistry reg5;
        // ReadFileTool has no parameters_json schema
        ToolCall call;
        call.name = "read_file";
        call.id = "call_4";
        call.arguments = R"({"path": "/tmp/test"})";
        auto result = reg5.execute(call, {}).get();
        // Should not be a validation error
        assert(result.output.find("Invalid arguments") == std::string::npos);
    }
    PASS();

    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
