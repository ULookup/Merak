#include <merak/tool_registry.hpp>
#include <merak/builtin_tools.hpp>
#include <merak/mcp_tool_wrapper.hpp>
#include <cassert>
#include <iostream>

using namespace merak;

int main() {
    // Test registration
    ToolRegistry registry;
    registry.register_tool(std::make_unique<tools::ReadFileTool>());
    assert(registry.size() == 1);
    assert(registry.find_spec("read_file").has_value());
    assert(!registry.find_spec("nonexistent").has_value());

    // Test duplicate registration
    registry.register_tool(std::make_unique<tools::ReadFileTool>());
    assert(registry.size() == 1);

    // Test spec
    auto spec = registry.find_spec("read_file").value();
    assert(spec.name == "read_file");
    assert(spec.source == "builtin");

    // Test permission check
    assert(registry.check_permission("read_file", "auto"));
    assert(registry.check_permission("read_file", "ask"));
    assert(!registry.check_permission("nonexistent", "auto"));

    // Test all_tools
    auto all = registry.all_tools();
    assert(all.size() == 1);

    // Test clone
    auto* tool = registry.get_tool("read_file");
    assert(tool != nullptr);
    auto cloned = tool->clone();
    assert(cloned->spec().name == "read_file");

    std::cout << "All tools tests passed!" << std::endl;
    return 0;
}
