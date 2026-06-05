#pragma once
#include <string>
#include <merak/tool_meta.hpp>

namespace merak {

// ——— 工具的说明书：名称 + 描述 + 参数 schema ———
struct ToolSpec {
    std::string name;            // 工具名，LLM 通过此名称调用
    std::string description;     // 自然语言描述，LLM 据此判断何时调用
    std::string parameters_json; // JSON Schema 格式的参数定义
    std::string source;          // "builtin" | "mcp://server_name"
    Category category = Category::ReadOnly;
};

} // namespace merak
