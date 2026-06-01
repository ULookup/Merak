#pragma once
#include <string>
#include <stdexcept>

namespace merak {

enum class ErrorType {
    NO_ERROR,
    CONFIG_ERROR,       // 配置文件缺失/格式错误
    LLM_ERROR,          // LLM API 网络/认证/限流错误
    LLM_TIMEOUT,        // LLM 调用超时
    TOOL_ERROR,         // 工具执行失败
    TOOL_NOT_FOUND,     // LLM 请求了不存在的工具
    MEMORY_ERROR,       // 记忆存储/检索失败
    MCP_ERROR,          // MCP Server 通信错误
    CONTEXT_OVERFLOW,   // 上下文超出 Token 上限
    INTERNAL_ERROR      // 未分类的内部错误
};

// ——— 统一的错误类型 ———
class AgentError : public std::runtime_error {
public:
    AgentError(ErrorType type, std::string message)
        : std::runtime_error(std::move(message))
        , error_type_(type) {}

    ErrorType type() const { return error_type_; }

private:
    ErrorType error_type_;
};

} // namespace merak
