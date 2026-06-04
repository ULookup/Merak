#pragma once
#include <merak/prompts/types.hpp>
#include <string>

namespace merak::prompts {

// 对标 astra builtin_markdown_skill() + builtin_concise_skill()
PromptSection build_skill_section();

} // namespace merak::prompts
