#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace merak {

class EditJournal {
public:
    struct Entry {
        std::filesystem::path path;
        std::string before;    // empty if file was new
        std::string after;     // empty if file was deleted
        std::chrono::steady_clock::time_point time;
    };

    void record(const std::filesystem::path& path,
                const std::string& before,
                const std::string& after);

    const std::vector<Entry>& entries() const { return journal_; }
    bool rollback(size_t count = 1);
    void clear() { journal_.clear(); }

private:
    std::vector<Entry> journal_;
    static constexpr size_t kMaxEntries = 100;
};

} // namespace merak
