#include "../src/tui/syntax/highlighter.hpp"
#include "../src/tui/syntax/languages.hpp"
#include "../src/tui/syntax/theme_map.hpp"
#include <cassert>
#include <iostream>

using namespace merak::tui::syntax;

static void test_language_lookup() {
    assert(language_for("python") != nullptr);
    assert(language_for("py") != nullptr);
    assert(language_for("cpp") != nullptr);
    assert(language_for("go") != nullptr);
    assert(language_for("rust") != nullptr);
    assert(language_for("javascript") != nullptr);
    assert(language_for("unknown") == nullptr);
}

static void test_highlight_python() {
    Highlighter h;
    auto* lang = language_for("python");
    auto spans = h.highlight("def foo():\n    return 42\n", lang);
    assert(!spans.empty());
}

static void test_theme_map_dark() {
    auto tm = ThemeMap::dark();
    auto s = tm.style_for(HighlightToken::KEYWORD);
    assert(s.bold());
}

int main() {
    test_language_lookup();
    test_highlight_python();
    test_theme_map_dark();
    std::cout << "All syntax tests passed.\n";
    return 0;
}
