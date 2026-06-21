#pragma once
#include <merak/message.hpp>
#include <string>
#include <vector>

namespace merak {

class TokenCounter {
public:
    explicit TokenCounter(const std::string& model = "gpt-4o")
        : model_(model)
    {
        if (model.find("gpt") != std::string::npos ||
            model.find("o1") != std::string::npos ||
            model.find("o3") != std::string::npos) {
            chars_per_token_ = 3.5;
        } else if (model.find("claude") != std::string::npos) {
            chars_per_token_ = 4.0;
        } else {
            chars_per_token_ = 3.5;
        }
    }

    int count(const Message& msg) const;
    int count(const std::vector<Message>& messages) const;
    int count(const std::string& text) const;

    int fit_in_budget(const std::vector<Message>& messages,
        int token_limit) const;

    // Update authoritative token count from API response.
    // Subsequent count() calls use this as baseline, only estimating
    // messages beyond the authoritative count.
    void update_authoritative(int prompt_tokens, int message_count) {
        authoritative_total_ = prompt_tokens;
        authoritative_message_count_ = message_count;
    }

private:
    std::string model_;
    double chars_per_token_;
    int authoritative_total_ = 0;
    int authoritative_message_count_ = 0;
};

} // namespace merak
