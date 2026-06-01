#pragma once
#include <string>
#include <vector>
#include <optional>
namespace merak {

// ——— 工具调用请求：LLM 决定调用某个工具 ———
struct ToolCall {
    std::string id;          // LLM 生成的唯一 ID，如 "call_abc123"
    std::string name;        // 工具名，如 "read_file"
    std::string arguments;   // JSON 字符串，如 {"path":"foo.cpp"}
};

// ——— 工具调用结果：工具执行完返回给 LLM ———
struct ToolResult {
    std::string call_id;     // 关联到 ToolCall.id
    std::string output;      // 工具输出文本（给 LLM 看的）
    bool is_error = false;   // 工具执行失败时为 true
};

// ——— 一条消息：对话中的一根线 ———
struct Message {
    std::string role;  // "system" | "user" | "assistant" | "tool"

    // 文本内容（user 消息是用户输入，assistant 消息是 LLM 回复，tool 消息是工具返回）
    std::string content;

    // 只有 assistant 角色时可能有值：LLM 决定要调用的工具列表
    std::vector<ToolCall> tool_calls;

    // 只有 tool 角色时有值：此消息是哪个 tool_call 的结果
    std::optional<std::string> tool_call_id;
};

// ——— 完整的一次 Agent 回复 ———
struct AgentResponse {
    std::string text;                      // LLM 最终的文本回复
    std::vector<ToolCall> tool_calls;      // LLM 请求的工具调用列表
    std::vector<ToolResult> tool_results;  // 此轮执行的所有工具结果
    int total_input_tokens = 0;            // 消耗的输入 token
    int total_output_tokens = 0;           // 消耗的输出 token
    bool has_usage = false;                // Provider returned exact usage data
    bool usage_missing = false;            // At least one provider response omitted usage
};

} // namespace merak
