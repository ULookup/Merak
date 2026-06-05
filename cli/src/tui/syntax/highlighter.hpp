#pragma once
#include "languages.hpp"
#include <tree_sitter/api.h>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace merak::tui::syntax {

enum class HighlightToken : uint8_t {
    TEXT, KEYWORD, STRING, NUMBER, COMMENT, TYPE, FUNCTION,
    METHOD, PROPERTY, VARIABLE, PARAMETER, OPERATOR, PUNCTUATION,
    CONSTANT, NAMESPACE, REGEX, ESCAPE, LABEL, ATTRIBUTE,
    EMBEDDED, TAG, ERROR, DIFF_ADD, DIFF_DEL
};

struct HighlightSpan {
    uint32_t start_byte;
    uint32_t end_byte;
    HighlightToken token;
};

class Highlighter {
    TSParser* parser_;
    const TSLanguage* language_ = nullptr;

public:
    Highlighter() {
        parser_ = ts_parser_new();
    }
    ~Highlighter() {
        ts_parser_delete(parser_);
    }

    void set_language(const TSLanguage* lang) {
        if (lang != language_) {
            ts_parser_set_language(parser_, lang);
            language_ = lang;
        }
    }

    std::vector<HighlightSpan> highlight(std::string_view code, const TSLanguage* lang) {
        std::vector<HighlightSpan> result;
        if (!lang) return result;
        set_language(lang);

        TSTree* tree = ts_parser_parse_string(parser_, nullptr,
            code.data(), static_cast<uint32_t>(code.size()));
        if (!tree) return result;

        TSNode root = ts_tree_root_node(tree);
        walk_tree(root, code, result);

        ts_tree_delete(tree);
        return result;
    }

    void update(std::string_view new_text) {
        if (!language_) return;
        TSTree* tree = ts_parser_parse_string(parser_, nullptr,
            new_text.data(), static_cast<uint32_t>(new_text.size()));
        if (tree) ts_tree_delete(tree);
    }

private:
    void walk_tree(TSNode node, std::string_view code, std::vector<HighlightSpan>& out) {
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        HighlightToken token = token_for_node(ts_node_type(node));

        if (token != HighlightToken::TEXT || ts_node_child_count(node) == 0) {
            if (start < end) {
                out.push_back({start, end, token});
            }
        }

        for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
            walk_tree(ts_node_child(node, i), code, out);
        }
    }

    static HighlightToken token_for_node(const char* type) {
        static const std::unordered_map<std::string_view, HighlightToken> map = {
            {"keyword", HighlightToken::KEYWORD},
            {"string", HighlightToken::STRING},
            {"number", HighlightToken::NUMBER},
            {"comment", HighlightToken::COMMENT},
            {"type", HighlightToken::TYPE},
            {"function", HighlightToken::FUNCTION},
            {"method", HighlightToken::METHOD},
            {"property", HighlightToken::PROPERTY},
            {"variable", HighlightToken::VARIABLE},
            {"parameter", HighlightToken::PARAMETER},
            {"operator", HighlightToken::OPERATOR},
            {"constant", HighlightToken::CONSTANT},
            {"regex", HighlightToken::REGEX},
            {"escape", HighlightToken::ESCAPE},
            {"label", HighlightToken::LABEL},
            {"attribute", HighlightToken::ATTRIBUTE},
            {"tag", HighlightToken::TAG},
            {"ERROR", HighlightToken::ERROR},
        };
        auto it = map.find(type);
        return it != map.end() ? it->second : HighlightToken::TEXT;
    }
};

} // namespace merak::tui::syntax
