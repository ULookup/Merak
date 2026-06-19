#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace merak::worldbuilding::prompts {

inline std::string load_writer_prompt(const std::filesystem::path& prompts_dir = "config/prompts") {
    static std::string cached;
    static bool loaded = false;
    if (!loaded) {
        std::ifstream file(prompts_dir / "worldbuilding" / "writer.md");
        if (file.is_open()) {
            std::ostringstream ss;
            ss << file.rdbuf();
            cached = ss.str();
        }
        loaded = true;
    }
    return cached;
}

} // namespace merak::worldbuilding::prompts
