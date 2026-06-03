#include <merak/edit_journal.hpp>
#include <fstream>

namespace merak {

void EditJournal::record(const std::filesystem::path& path,
                         const std::string& before,
                         const std::string& after) {
    journal_.push_back({path, before, after, std::chrono::steady_clock::now()});
    if (journal_.size() > kMaxEntries) journal_.erase(journal_.begin());
}

bool EditJournal::rollback(size_t count) {
    if (count > journal_.size()) return false;
    for (size_t i = 0; i < count; ++i) {
        auto& entry = journal_[journal_.size() - 1 - i];
        if (entry.before.empty()) {
            std::filesystem::remove(entry.path);
        } else {
            std::ofstream out(entry.path);
            out << entry.before;
        }
    }
    journal_.resize(journal_.size() - count);
    return true;
}

} // namespace merak
