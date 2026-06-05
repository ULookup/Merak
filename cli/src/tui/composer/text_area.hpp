#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace merak::tui {

class TextArea {
    std::string text_;
    size_t cursor_ = 0;
    std::string kill_buffer_;

    size_t line_start() const {
        auto pos = text_.rfind('\n', cursor_ == 0 ? 0 : cursor_ - 1);
        return pos == std::string::npos ? 0 : pos + 1;
    }
    size_t line_end() const {
        auto pos = text_.find('\n', cursor_);
        return pos == std::string::npos ? text_.size() : pos;
    }

public:
    const std::string& text() const { return text_; }
    size_t cursor() const { return cursor_; }
    bool empty() const { return text_.empty(); }
    void clear() { text_.clear(); cursor_ = 0; }
    void set_text(std::string text) { text_ = std::move(text); cursor_ = text_.size(); }
    void insert(std::string_view value) { text_.insert(cursor_, value); cursor_ += value.size(); }
    void insert_char(char c) { text_.insert(text_.begin() + static_cast<long>(cursor_), c); ++cursor_; }
    void newline() { insert_char('\n'); }
    void replace_range(size_t start, size_t end, std::string_view value) {
        if (start > text_.size()) start = text_.size();
        if (end > text_.size()) end = text_.size();
        if (end < start) end = start;
        text_.replace(start, end - start, value);
        cursor_ = start + value.size();
    }

    void backspace() {
        if (cursor_ == 0) return;
        text_.erase(cursor_ - 1, 1);
        --cursor_;
    }
    void delete_forward() {
        if (cursor_ < text_.size()) text_.erase(cursor_, 1);
    }
    void move_left() { if (cursor_ > 0) --cursor_; }
    void move_right() { if (cursor_ < text_.size()) ++cursor_; }
    void move_home() { cursor_ = line_start(); }
    void move_end() { cursor_ = line_end(); }
    void move_word_left() {
        while (cursor_ > 0 && std::isspace(static_cast<unsigned char>(text_[cursor_ - 1]))) --cursor_;
        while (cursor_ > 0 && !std::isspace(static_cast<unsigned char>(text_[cursor_ - 1]))) --cursor_;
    }
    void move_word_right() {
        while (cursor_ < text_.size() && !std::isspace(static_cast<unsigned char>(text_[cursor_]))) ++cursor_;
        while (cursor_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[cursor_]))) ++cursor_;
    }
    void kill_to_start() {
        const auto start = line_start();
        kill_buffer_ = text_.substr(start, cursor_ - start);
        text_.erase(start, cursor_ - start);
        cursor_ = start;
    }
    void kill_to_end() {
        const auto end = line_end();
        kill_buffer_ = text_.substr(cursor_, end - cursor_);
        text_.erase(cursor_, end - cursor_);
    }
    void delete_word_left() {
        const auto old = cursor_;
        move_word_left();
        kill_buffer_ = text_.substr(cursor_, old - cursor_);
        text_.erase(cursor_, old - cursor_);
    }
    void yank() { insert(kill_buffer_); }

    std::vector<std::string> lines() const {
        std::vector<std::string> result;
        size_t start = 0;
        while (start <= text_.size()) {
            auto end = text_.find('\n', start);
            if (end == std::string::npos) end = text_.size();
            result.push_back(text_.substr(start, end - start));
            if (end == text_.size()) break;
            start = end + 1;
        }
        return result;
    }
};

} // namespace merak::tui
