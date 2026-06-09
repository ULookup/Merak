#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <merak/error.hpp>
#include <merak/result.hpp>
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <cassert>
#include <iostream>

using namespace merak;

int main() {
    // Test Message
    {
        Message msg;
        msg.role = "user";
        msg.content = "hello";
        assert(msg.role == "user");
        assert(msg.content == "hello");
        assert(msg.tool_calls.empty());
        assert(!msg.tool_call_id.has_value());
    }

    // Test ToolCall
    {
        ToolCall tc;
        tc.id = "call_123";
        tc.name = "read_file";
        tc.arguments = R"({"path":"foo.cpp"})";
        assert(tc.id == "call_123");
        assert(tc.name == "read_file");
    }

    // Test ToolResult
    {
        ToolResult tr;
        tr.call_id = "call_123";
        tr.output = "file contents";
        tr.is_error = false;
        assert(tr.call_id == "call_123");
        assert(!tr.is_error);
    }

    // Test AgentResponse with tool_calls
    {
        AgentResponse resp;
        resp.text = "Let me read that file";
        ToolCall tc{"call_abc", "read_file", R"({"path":"foo.cpp"})"};
        resp.tool_calls.push_back(tc);
        assert(resp.tool_calls.size() == 1);
        assert(resp.tool_calls[0].name == "read_file");
    }

    // Test AgentError
    {
        AgentError err(ErrorType::LLM_ERROR, "timeout");
        assert(err.type() == ErrorType::LLM_ERROR);
        assert(std::string(err.what()) == "timeout");
    }

    // Test ToolSpec
    {
        ToolSpec spec;
        spec.name = "test_tool";
        spec.description = "does things";
        spec.source = "builtin";
        assert(spec.name == "test_tool");
    }

    // Test Result<T,E>
    {
        Result<int, AgentError> ok(42);
        assert(ok.has_value());
        assert(!ok.has_error());
        assert(ok.value() == 42);
        assert(ok.value_or(0) == 42);
    }
    {
        Result<int, AgentError> err(AgentError(ErrorType::INTERNAL_ERROR, "fail"));
        assert(!err.has_value());
        assert(err.has_error());
        assert(err.value_or(100) == 100);
    }

    std::cout << "All core tests passed!" << std::endl;
    return 0;
}
