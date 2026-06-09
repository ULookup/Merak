#include <merak/session_journal.hpp>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace merak {

SessionJournal::SessionJournal(const std::filesystem::path& dir) {
  std::filesystem::create_directories(dir);
  file_path_ = dir / "session.jsonl";
  file_.open(file_path_, std::ios::app);
}

SessionJournal::~SessionJournal() {
  if (file_.is_open()) file_.close();
}

void SessionJournal::append(const Event& event) {
  append(event.event_type, event.payload);
}

void SessionJournal::append(const std::string& event_type, const nlohmann::json& payload) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!file_.is_open()) return;

  auto now = std::chrono::system_clock::now();
  auto time_t_val = std::chrono::system_clock::to_time_t(now);
  auto tm_val = *std::gmtime(&time_t_val);

  std::ostringstream ts;
  ts << std::put_time(&tm_val, "%Y-%m-%dT%H:%M:%SZ");

  nlohmann::json entry;
  entry["ts"] = ts.str();
  entry["event"] = event_type;
  entry["payload"] = payload;

  file_ << entry.dump() << "\n";
  file_.flush();
}

} // namespace merak
