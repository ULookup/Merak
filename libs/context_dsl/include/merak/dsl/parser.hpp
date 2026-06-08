#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace merak::dsl {

struct DslRef {
    std::string type; // "agent", "scene", "chapter", "arc", "foreshadow", "secret", "world", "diary", "relation"
    std::unordered_map<std::string, std::string> params;
    std::string raw; // raw @xxx{...} text
};

class Parser {
public:
    static std::vector<DslRef> parse(const std::string& template_text);

private:
    static std::unordered_map<std::string, std::string> parse_params(const std::string& params_str);
};

} // namespace merak::dsl
