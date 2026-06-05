#pragma once
#include <tree_sitter/api.h>
#include <string_view>
#include <unordered_map>

namespace merak::tui::syntax {

extern "C" {
    const TSLanguage* tree_sitter_cpp();
    const TSLanguage* tree_sitter_python();
    const TSLanguage* tree_sitter_go();
    const TSLanguage* tree_sitter_javascript();
    const TSLanguage* tree_sitter_typescript();
    const TSLanguage* tree_sitter_rust();
    const TSLanguage* tree_sitter_bash();
    const TSLanguage* tree_sitter_json();
    const TSLanguage* tree_sitter_yaml();
    const TSLanguage* tree_sitter_markdown();
    const TSLanguage* tree_sitter_sql();
    const TSLanguage* tree_sitter_java();
    const TSLanguage* tree_sitter_c_sharp();
    const TSLanguage* tree_sitter_ruby();
}

inline const TSLanguage* language_for(std::string_view name) {
    static const std::unordered_map<std::string_view, const TSLanguage*(*)()> map = {
        {"cpp", tree_sitter_cpp}, {"c++", tree_sitter_cpp}, {"c", tree_sitter_cpp},
        {"python", tree_sitter_python}, {"py", tree_sitter_python},
        {"go", tree_sitter_go}, {"golang", tree_sitter_go},
        {"javascript", tree_sitter_javascript}, {"js", tree_sitter_javascript},
        {"typescript", tree_sitter_typescript}, {"ts", tree_sitter_typescript},
        {"rust", tree_sitter_rust}, {"rs", tree_sitter_rust},
        {"bash", tree_sitter_bash}, {"sh", tree_sitter_bash}, {"shell", tree_sitter_bash},
        {"json", tree_sitter_json},
        {"yaml", tree_sitter_yaml}, {"yml", tree_sitter_yaml},
        {"markdown", tree_sitter_markdown}, {"md", tree_sitter_markdown},
        {"sql", tree_sitter_sql},
        {"java", tree_sitter_java},
        {"csharp", tree_sitter_c_sharp}, {"c#", tree_sitter_c_sharp}, {"cs", tree_sitter_c_sharp},
        {"ruby", tree_sitter_ruby}, {"rb", tree_sitter_ruby},
    };
    auto it = map.find(name);
    return it != map.end() ? it->second() : nullptr;
}

inline bool is_supported(std::string_view name) {
    return language_for(name) != nullptr;
}

} // namespace merak::tui::syntax
