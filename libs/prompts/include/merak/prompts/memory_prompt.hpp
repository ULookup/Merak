#pragma once
#include <merak/prompts/types.hpp>
#include <string>

namespace merak::prompts {

// 对标 astra build_memory_prompt(MemoryPromptMode)
PromptSection build_memory_section(MemoryPromptMode mode);

} // namespace merak::prompts
