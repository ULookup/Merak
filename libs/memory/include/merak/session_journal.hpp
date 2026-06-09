#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <mutex>
#include <fstream>
#include <filesystem>

namespace merak {

class SessionJournal {
public:
  struct Event {
    std::string event_type;
    nlohmann::json payload;
  };

  explicit SessionJournal(const std::filesystem::path& dir);
  ~SessionJournal();

  void append(const Event& event);
  void append(const std::string& event_type, const nlohmann::json& payload);

  const std::filesystem::path& path() const { return file_path_; }

private:
  std::filesystem::path file_path_;
  std::ofstream file_;
  std::mutex mutex_;
};

} // namespace merak
