#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace merak::tui {

struct MentionCandidate {
    std::string path;
    int score = 0;
};

inline std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

inline int fuzzy_path_score(const std::string& path, const std::string& query) {
    auto p = lower_copy(path);
    auto q = lower_copy(query);
    if (q.empty()) return 1;
    if (p.starts_with(q)) return 10000 - static_cast<int>(p.size());
    if (p.find(q) != std::string::npos) return 5000 - static_cast<int>(p.find(q));
    int score = 0;
    size_t at = 0;
    for (char c : q) {
        at = p.find(c, at);
        if (at == std::string::npos) return 0;
        score += 10;
        ++at;
    }
    return score;
}

class FileProvider {
    std::filesystem::path root_;
    mutable std::vector<std::string> cache_;

    static bool skip_dir(const std::filesystem::path& path) {
        auto name = path.filename().string();
        return name == ".git" || name == "build" || name == ".cache" || name == "node_modules"
            || name == ".conan2" || name == ".superpowers"
            || (path.string().find(".claude/worktrees") != std::string::npos);
    }

public:
    explicit FileProvider(std::filesystem::path root = std::filesystem::current_path())
        : root_(std::move(root)) {}

    std::vector<std::string> files() const {
        if (!cache_.empty()) return cache_;
        std::vector<std::string> out;
        if (!std::filesystem::exists(root_)) return out;
        std::filesystem::recursive_directory_iterator it(root_), end;
        while (it != end) {
            const auto& entry = *it;
            if (entry.is_directory() && skip_dir(entry.path())) {
                it.disable_recursion_pending();
                ++it;
                continue;
            }
            if (entry.is_regular_file()) {
                auto rel = std::filesystem::relative(entry.path(), root_).generic_string();
                if (!rel.empty() && rel[0] != '.') out.push_back(rel);
            }
            ++it;
        }
        std::sort(out.begin(), out.end());
        cache_ = out;
        return cache_;
    }
};

class MentionMenu {
    FileProvider provider_;
    std::vector<MentionCandidate> matches_;
    size_t selected_ = 0;
    size_t trigger_start_ = std::string::npos;

public:
    explicit MentionMenu(FileProvider provider = FileProvider()) : provider_(std::move(provider)) {}
    const std::vector<MentionCandidate>& matches() const { return matches_; }
    size_t trigger_start() const { return trigger_start_; }
    bool open() const { return trigger_start_ != std::string::npos && !matches_.empty(); }

    bool update_from(const std::string& text, size_t cursor) {
        matches_.clear();
        trigger_start_ = std::string::npos;
        if (cursor > text.size()) cursor = text.size();
        auto at = text.rfind('@', cursor == 0 ? 0 : cursor - 1);
        if (at == std::string::npos) return false;
        if (at > 0 && !std::isspace(static_cast<unsigned char>(text[at - 1]))) return false;
        auto query = text.substr(at + 1, cursor - at - 1);
        if (query.find_first_of(" \n\t") != std::string::npos) return false;
        for (const auto& file : provider_.files()) {
            auto score = fuzzy_path_score(file, query);
            if (score > 0) matches_.push_back({file, score});
        }
        std::sort(matches_.begin(), matches_.end(), [](const auto& a, const auto& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.path < b.path;
        });
        if (matches_.size() > 8) matches_.resize(8);
        selected_ = 0;
        trigger_start_ = at;
        return !matches_.empty();
    }

    void next() { if (!matches_.empty()) selected_ = (selected_ + 1) % matches_.size(); }
    void prev() { if (!matches_.empty()) selected_ = (selected_ + matches_.size() - 1) % matches_.size(); }
    std::string accepted_text() const {
        if (matches_.empty()) return "";
        return "@" + matches_[selected_].path + " ";
    }
};

} // namespace merak::tui
