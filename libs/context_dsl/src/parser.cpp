#include <merak/dsl/parser.hpp>

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace merak::dsl {

std::unordered_map<std::string, std::string> Parser::parse_params(const std::string& params_str) {
    std::unordered_map<std::string, std::string> result;
    if (params_str.empty()) return result;

    // Split by comma
    std::string::size_type start = 0;
    while (start < params_str.size()) {
        // Find next comma (or end of string)
        auto comma = params_str.find(',', start);
        std::string pair = (comma == std::string::npos)
                               ? params_str.substr(start)
                               : params_str.substr(start, comma - start);

        // Trim whitespace
        auto trim = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }).base(),
                    s.end());
        };
        trim(pair);

        // Split by first '='
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key = pair.substr(0, eq);
            std::string value = pair.substr(eq + 1);
            trim(key);
            trim(value);
            if (!key.empty()) {
                result[key] = value;
            }
        } else if (!pair.empty()) {
            // No '=' sign — treat the whole thing as a flag (value = "true")
            result[pair] = "true";
        }

        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    return result;
}

std::vector<DslRef> Parser::parse(const std::string& template_text) {
    std::vector<DslRef> refs;

    // Regex: @(type){params}
    static const std::regex dsl_regex(
        R"(@(agent|scene|chapter|arc|foreshadow|secret|world|diary|relation|graph_subgraph|graph_expand|graph_path)\{([^}]*)\})");

    auto begin = std::sregex_iterator(template_text.begin(), template_text.end(), dsl_regex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        DslRef ref;
        ref.type = (*it)[1].str();
        ref.params = parse_params((*it)[2].str());
        ref.raw = it->str();
        refs.push_back(std::move(ref));
    }

    return refs;
}

} // namespace merak::dsl
