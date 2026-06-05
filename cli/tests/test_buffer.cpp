#include "../src/tui/buffer.hpp"
#include <cassert>
#include <iostream>

using namespace merak::tui;

static void test_cell_default() {
    Cell c;
    assert(c.ch == U' ');
    assert(c.style == Style{});
    assert(c.width == 1);
}

static void test_buffer_resize() {
    Buffer buf;
    buf.resize(10, 3);
    assert(buf.w == 10);
    assert(buf.h == 3);
    assert(buf.cells.size() == 30);
}

static void test_buffer_at() {
    Buffer buf;
    buf.resize(5, 2);
    buf.at(2, 1).ch = U'X';
    assert(buf.at(2, 1).ch == U'X');
}

static void test_buffer_set_span_ascii() {
    Buffer buf;
    buf.resize(10, 1);
    Style s; s.fg = 100;
    buf.set_span(0, 0, "hello", s);
    assert(buf.at(0, 0).ch == U'h');
    assert(buf.at(4, 0).ch == U'o');
    assert(buf.at(0, 0).style.fg == 100);
}

static void test_buffer_set_span_cjk() {
    Buffer buf;
    buf.resize(10, 1);
    buf.set_span(0, 0, "你好", Style{});
    assert(buf.at(0, 0).ch == U'你');
    assert(buf.at(0, 0).width == 2);
    assert(buf.at(1, 0).ch == U'好');
    assert(buf.at(1, 0).width == 2);
}

static void test_buffer_diff_no_change() {
    Buffer a, b;
    a.resize(5, 2);
    b.resize(5, 2);
    auto diffs = b.diff(a);
    assert(diffs.empty());
}

static void test_buffer_diff_one_change() {
    Buffer a, b;
    a.resize(5, 2);
    b.resize(5, 2);
    b.at(2, 1).ch = U'X';
    auto diffs = b.diff(a);
    assert(diffs.size() == 1);
    assert(diffs[0].x == 2);
    assert(diffs[0].y == 1);
    assert(diffs[0].cell.ch == U'X');
}

static void test_char_width() {
    assert(char_width(U'a') == 1);
    assert(char_width(U'你') == 2);
    assert(char_width(U' ') == 1);
    // combining accent
    assert(char_width(0x0301) == 0);
}

static void test_utf8_roundtrip() {
    std::string input = "Hello 世界 🌍";
    size_t pos = 0;
    std::string output;
    while (pos < input.size()) {
        auto cp = utf8_decode(input, pos);
        utf8_encode(cp, output);
    }
    assert(input == output);
}

int main() {
    test_cell_default();
    test_buffer_resize();
    test_buffer_at();
    test_buffer_set_span_ascii();
    test_buffer_set_span_cjk();
    test_buffer_diff_no_change();
    test_buffer_diff_one_change();
    test_char_width();
    test_utf8_roundtrip();
    std::cout << "All buffer tests passed.\n";
    return 0;
}
