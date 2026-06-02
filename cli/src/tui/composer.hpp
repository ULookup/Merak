#pragma once
#include <deque>
#include <string>

namespace merak::tui {

enum class SubmitKind { Empty, Immediate, Queued };

class Composer {
    std::string text_;
    std::deque<std::string> queued_;
public:
    const std::string& text() const { return text_; }
    void set_text(std::string text) { text_ = std::move(text); }
    bool empty() const { return text_.empty(); }
    void append(const std::string& text) { text_ += text; }
    void backspace() { if (!text_.empty()) text_.pop_back(); }
    size_t queued_count() const { return queued_.size(); }
    SubmitKind submit(bool turn_active) {
        if (text_.empty()) return SubmitKind::Empty;
        if (turn_active) {
            queued_.push_back(std::move(text_));
            text_.clear();
            return SubmitKind::Queued;
        }
        return SubmitKind::Immediate;
    }
    std::string take_immediate() {
        auto text = std::move(text_);
        text_.clear();
        return text;
    }
    bool edit_last_queued() {
        if (queued_.empty() || !text_.empty()) return false;
        text_ = std::move(queued_.back());
        queued_.pop_back();
        return true;
    }
    bool refill_next_queued() {
        if (queued_.empty() || !text_.empty()) return false;
        text_ = std::move(queued_.front());
        queued_.pop_front();
        return true;
    }
};

} // namespace merak::tui
