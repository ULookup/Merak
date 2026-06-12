#include <merak/fork_skill_tool.hpp>
#include <merak/agent_loop.hpp>
#include <merak/tool_registry.hpp>
#include <merak/token_counter.hpp>
#include <merak/compactor.hpp>

namespace merak::tools {

ForkSkillTool::ForkSkillTool(
    skills::SkillDef def,
    std::shared_ptr<LlmProvider> llm,
    std::shared_ptr<ToolRegistry> tools,
    std::shared_ptr<MemoryStore> memory)
    : skill_(std::move(def))
    , llm_(std::move(llm))
    , tools_(std::move(tools))
    , memory_(std::move(memory))
{}

ToolSpec ForkSkillTool::spec() const {
    ToolSpec s;
    s.name = "skill:" + skill_.name;
    s.description = skill_.description;
    s.source = "builtin";
    s.category = Category::ReadOnly;
    return s;
}

ToolMeta ForkSkillTool::meta() const {
    ToolMeta m;
    m.name = "skill:" + skill_.name;
    m.description = skill_.description;
    m.pinned = true;
    m.intents = {IntentType::AgentOp};
    return m;
}

PermissionLevel ForkSkillTool::permission() const {
    return PermissionLevel::safe;
}

std::future<ToolResult> ForkSkillTool::execute(
    ToolCall call, ToolExecutionContext context) {
    return std::async(std::launch::async,
        [this, call = std::move(call), context = std::move(context)]() -> ToolResult {

        auto sub_tools = std::make_shared<ToolRegistry>();
        for (auto& name : skill_.allowed_tools) {
            auto* tool = tools_->get_tool(name);
            if (tool) {
                sub_tools->register_tool(tool->clone());
            }
        }

        auto counter = std::make_shared<TokenCounter>();
        auto comp = std::make_shared<Compactor>(llm_, counter);

        AgentLoop::Config cfg;
        cfg.system_prompt = skill_.body;
        cfg.max_turns = 5;
        cfg.default_model = "gpt-4o";
        cfg.max_output_tokens = 4096;
        cfg.model_max_tokens = 16000;
        cfg.enable_compaction = false;
        cfg.enable_cache = true;

        AgentLoop sub_loop(cfg, llm_, sub_tools, memory_, comp, nullptr, nullptr);

        NullRunControl control;
        try {
            auto response = sub_loop.run(call.arguments, control).get();
            ToolResult result;
            result.call_id = call.id;
            result.output = response.text;
            result.is_error = false;
            return result;
        } catch (const std::exception& e) {
            ToolResult result;
            result.call_id = call.id;
            result.output = std::string("Fork skill '") + skill_.name + "' failed: " + e.what();
            result.is_error = true;
            return result;
        }
    });
}

std::unique_ptr<Tool> ForkSkillTool::clone() const {
    return std::make_unique<ForkSkillTool>(skill_, llm_, tools_, memory_);
}

} // namespace merak::tools
