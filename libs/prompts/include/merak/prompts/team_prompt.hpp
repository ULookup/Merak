#pragma once
#include <merak/prompts/types.hpp>
#include <string>
#include <optional>

namespace merak::prompts {

// 对标 astra team_prompts 的各个函数
std::string build_team_coordination(const TeamContext& ctx);
std::string build_budget_awareness(const ResourceBudget& budget);

} // namespace merak::prompts
