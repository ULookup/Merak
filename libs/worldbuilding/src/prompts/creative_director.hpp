#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace merak::worldbuilding::prompts {

inline std::string load_creative_director(const std::filesystem::path& prompts_dir = "config/prompts") {
    std::ifstream file(prompts_dir / "worldbuilding" / "creative_director.md");
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace merak::worldbuilding::prompts
