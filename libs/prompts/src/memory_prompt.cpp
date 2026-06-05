#include <merak/prompts/memory_prompt.hpp>
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>

namespace merak::prompts {

namespace {

std::string load_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        spdlog::warn("prompts: cannot load file {}", path);
        return "";
    }
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

const char* MINIMAL_RULES = R"(
仅在用户陈述了明确的持久偏好、纠正、决策或项目事实时存储。
- 偏向不存储：记漏比记错划算。
- 不询问权限——直接调用 memory 工具。
- 不存储临时状态。
- 不为了有东西可存而去探索代码库找理由。
)";

} // namespace

PromptSection build_memory_section(MemoryPromptMode mode) {
    if (mode == MemoryPromptMode::None) {
        return {"", CacheScope::Session};
    }

    if (mode == MemoryPromptMode::Minimal) {
        return {std::string("## Memory 管理规则\n") + MINIMAL_RULES, CacheScope::Session};
    }

    // Full 模式：加载文件
    std::string content = load_file("config/prompts/memory/memory.md");
    return {content, CacheScope::Session};
}

} // namespace merak::prompts
