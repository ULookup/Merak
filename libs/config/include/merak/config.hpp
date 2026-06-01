#pragma once
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <merak/message.hpp> // 需要用到 Message 类型

namespace merak {

struct ThinkingConfig {
    std::string type = "disabled"; // "disabled" | "adaptive" | "enabled"
    std::string effort;            // Optional: "low" | "medium" | "high" | "xhigh" | "max"
    int budget_tokens = 0;         // Required for manual "enabled" thinking
};

// ——— LLM Provider 配置 ———
// 一个 Provider 就是一个 LLM 服务端点
struct LLMConfig {
    std::string provider;      // "openai" 或 "anthropic"，由 default_config() 设置
    std::string api_key;
    std::string api_base_url;  // 由 default_config() / apply_provider_defaults() 设置
    std::string default_model; // 由 default_config() / apply_provider_defaults() 设置
    int max_output_tokens = 0;
    int request_timeout_ms = 60000;
    int max_retries = 3;
    std::optional<ThinkingConfig> thinking;
};

// ——— 模型配置 ———
// 为什么模型是单独的结构体而不是放在 LLMConfig 里？
// 因为一个 Provider 可以服务多个模型，Agent 可能在不同场景用不同模型
// 比如：主循环用 opus，压缩用 haiku（省钱）
struct ModelEntry {
    std::string name;            // 显示名称，如 "gpt-4o"
    std::string provider;        // 使用的 provider，如 "openai"
    int max_context_tokens = 128000; // 此模型的最大上下文长度
};

// ——— 长期记忆配置 ———
struct MemoryConfig {
    bool enabled = true;
    std::string db_connection;   // PostgreSQL 连接串
    std::string embedding_model = "text-embedding-3-small";
    int top_k_retrieval = 5;     // 语义检索返回数量
    float confidence_decay = 0.01;   // 每次衰减的步长
    int decay_interval_days = 7;     // 衰减间隔
};

// ——— MCP Server 配置 ———
// 每个 MCP Server 是一个外部进程，Agent 通过 stdio 和它通信
struct MCPServerConfig {
    std::string name;            // 显示名称
    std::string command;         // 启动命令
    std::vector<std::string> args; // 命令行参数
    std::map<std::string, std::string> env; // 环境变量（如 API Key）
    bool enabled = true;
};

// ——— Agent 核心配置 ———
struct AgentConfig {
    std::string system_prompt;       // Agent 的角色定义
    std::string default_model;       // 默认使用哪个模型
    int max_tool_turns = 25;         // 最多几轮工具调用
    double reserve_ratio = 0.15;     // 预留给 LLM 响应的 Token 比例
    double memory_budget_ratio = 0.20; // 记忆检索最多占 context 的百分比

    // 权限模式: "auto" | "ask" | "deny"
    std::string permission_mode = "ask";

    // 子 Agent 定义（多 Agent 场景）
    std::map<std::string, struct SubAgentConfig> sub_agents;
};

struct SubAgentConfig {
    std::string id;             // Agent 唯一标识
    std::string system_prompt;
    std::vector<std::string> tool_allowlist;
    std::string model;
    bool can_delegate = false;
};

// ——— 顶层配置 ———
struct Config {
    LLMConfig llm;
    std::vector<ModelEntry> models;
    MemoryConfig memory;
    std::vector<MCPServerConfig> mcp_servers;
    AgentConfig agent;
};

} // namespace merak
