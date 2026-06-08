#include <merak/skills/skill_loader.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace merak::skills {

std::expected<SkillDef, std::string> SkillLoader::load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::unexpected("skill_loader: failed to open file: " + path.string());
    }

    SkillDef skill;
    skill.source_path = path;

    std::string line;
    std::string current_key;
    bool in_frontmatter = false;
    bool frontmatter_done = false;
    bool first_delim = false;
    std::ostringstream body_stream;

    while (std::getline(file, line)) {
        // Trim trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (!first_delim) {
            // Look for first ---
            if (line == "---") {
                first_delim = true;
                in_frontmatter = true;
            }
            // Lines before first --- are ignored
            continue;
        }

        if (in_frontmatter && !frontmatter_done) {
            // Look for closing ---
            if (line == "---") {
                frontmatter_done = true;
                in_frontmatter = false;
                continue;
            }

            // Empty lines in frontmatter are skipped
            if (line.empty()) continue;

            // Check if this line is a list item (starts with whitespace + "-")
            auto stripped = [](const std::string& s) -> std::string {
                auto start = s.find_first_not_of(" \t");
                if (start == std::string::npos) return "";
                return s.substr(start);
            }(line);

            if (!current_key.empty() && !stripped.empty() && stripped[0] == '-') {
                // List item for current_key
                auto dash_pos = stripped.find('-');
                std::string value = stripped.substr(dash_pos + 1);
                // trim leading space
                auto val_start = value.find_first_not_of(" \t");
                if (val_start != std::string::npos) {
                    value = value.substr(val_start);
                }
                if (current_key == "allowed_tools") {
                    skill.allowed_tools.push_back(value);
                }
                continue;
            }

            // Otherwise, parse key: value pair
            auto colon_pos = line.find(':');
            if (colon_pos == std::string::npos) {
                return std::unexpected("skill_loader: malformed frontmatter line in " + path.string());
            }

            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);

            // trim key
            auto k_end = key.find_last_not_of(" \t");
            if (k_end != std::string::npos) {
                key = key.substr(0, k_end + 1);
            }

            // trim value
            auto v_start = value.find_first_not_of(" \t");
            if (v_start != std::string::npos) {
                value = value.substr(v_start);
            }

            current_key = key;

            if (key == "name") {
                skill.name = value;
            } else if (key == "version") {
                skill.version = value;
            } else if (key == "description") {
                skill.description = value;
            } else if (key == "context") {
                skill.context_mode = value;
            } else if (key == "allowed_tools") {
                // If there's a value on the same line as "allowed_tools:", capture it
                if (!value.empty()) {
                    skill.allowed_tools.push_back(value);
                }
                // Otherwise, subsequent lines with "- item" will be appended
            }
        } else {
            // Body lines - after frontmatter is done
            body_stream << line << '\n';
        }
    }

    if (!frontmatter_done) {
        return std::unexpected("skill_loader: missing frontmatter closing delimiter in " + path.string());
    }

    if (skill.name.empty()) {
        return std::unexpected("skill_loader: missing 'name' field in frontmatter of " + path.string());
    }

    skill.body = body_stream.str();
    // Remove trailing newline if present
    if (!skill.body.empty() && skill.body.back() == '\n') {
        skill.body.pop_back();
    }

    return skill;
}

} // namespace merak::skills
