#pragma once
#include <merak/prompts/types.hpp>
#include <string>
#include <vector>

namespace merak::prompts {

std::vector<PromptSection> build_core_sections(const PromptProfile& profile);

} // namespace merak::prompts
