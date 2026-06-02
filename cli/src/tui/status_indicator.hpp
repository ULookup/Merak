#pragma once
#include <chrono>
#include <string>

namespace merak::tui {

class StatusIndicator {
public:
    using Clock = std::chrono::steady_clock;

private:
    bool active_ = false;
    Clock::time_point turn_started_at_{};
    std::string activity_;
    size_t stream_chars_ = 0;

    static std::string duration(std::chrono::seconds value) {
        return std::to_string(value.count()) + "s";
    }

public:
    void begin_turn(Clock::time_point at = Clock::now()) {
        active_ = true;
        turn_started_at_ = at;
        activity_.clear();
        stream_chars_ = 0;
    }
    void finish_turn() {
        active_ = false;
        activity_.clear();
        stream_chars_ = 0;
    }
    bool active() const { return active_; }
    void set_activity(std::string activity) { activity_ = std::move(activity); }
    void bump_stream_chars(size_t amount) { stream_chars_ += amount; }
    std::string line_at(Clock::time_point now) const {
        if (!active_) return "";
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - turn_started_at_);
        auto tick = std::chrono::duration_cast<std::chrono::milliseconds>(now - turn_started_at_).count() / 250;
        static constexpr const char* frames[] = {"*", "+", "x", "+"};
        auto frame = frames[tick % 4];
        std::string out = std::string(frame) + " Thinking (" + duration(elapsed);
        if (!activity_.empty()) out += " | " + activity_;
        if (stream_chars_ == 0 && elapsed >= std::chrono::seconds(5)) {
            out += " | thought for " + duration(elapsed);
        }
        if (stream_chars_ > 0) out += " | ~" + std::to_string((stream_chars_ + 3) / 4) + " tokens";
        return out + " | Ctrl+C interrupt)";
    }
    std::string line() const { return line_at(Clock::now()); }
};

} // namespace merak::tui
