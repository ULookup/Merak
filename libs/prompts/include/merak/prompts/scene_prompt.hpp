#pragma once
#include <merak/prompts/types.hpp>
#include <string>
#include <vector>

namespace merak::prompts {

// Worldbuilding 场景上下文注入
PromptSection build_god_scene_context(const SceneContext& ctx);
PromptSection build_character_scene_context(const SceneContext& ctx);
PromptSection build_manager_scene_context(const SceneContext& ctx);

} // namespace merak::prompts
