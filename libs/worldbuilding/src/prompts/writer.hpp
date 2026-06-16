#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace merak::worldbuilding::prompts {

inline std::string load_writer_prompt(const std::filesystem::path& prompts_dir = "config/prompts") {
    std::ifstream file(prompts_dir / "worldbuilding" / "writer.md");
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace merak::worldbuilding::prompts
