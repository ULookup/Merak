#pragma once
#include <merak/tool_base.hpp>
#include <merak/skills/skill_types.hpp>
#include <memory>

namespace merak {

class LlmProvider;
class ToolRegistry;
class MemoryStore;

namespace tools {

class ForkSkillTool : public Tool {
public:
    ForkSkillTool(
        skills::SkillDef def,
        std::shared_ptr<LlmProvider> llm,
        std::shared_ptr<ToolRegistry> tools,
        std::shared_ptr<MemoryStore> memory);

    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(
        ToolCall call, ToolExecutionContext context) override;
    std::unique_ptr<Tool> clone() const override;

private:
    skills::SkillDef skill_;
    std::shared_ptr<LlmProvider> llm_;
    std::shared_ptr<ToolRegistry> tools_;
    std::shared_ptr<MemoryStore> memory_;
};

} // namespace tools
} // namespace merak
