#include <merak/spill_store.hpp>
#include <fstream>
#include <sstream>
#include <functional>
#include <spdlog/spdlog.h>

namespace merak {

SpillStore::SpillStore(const std::filesystem::path& base_dir, size_t max_bytes)
  : base_dir_(base_dir), max_total_bytes_(max_bytes) {
  std::filesystem::create_directories(base_dir_);
  recompute_total();
  spdlog::debug("SpillStore: {} initial={} bytes, max={} bytes",
                base_dir_.string(), current_total_bytes_, max_total_bytes_);
}

void SpillStore::recompute_total() {
  current_total_bytes_ = 0;
  if (!std::filesystem::exists(base_dir_)) return;
  for (auto& entry : std::filesystem::directory_iterator(base_dir_)) {
    if (entry.is_regular_file()) {
      current_total_bytes_ += entry.file_size();
    }
  }
}

std::optional<SpillReference> SpillStore::spill(SectionKind kind,
                                                  const std::string& content,
                                                  int turn_index) {
  if (kind == SectionKind::Identity ||
      kind == SectionKind::Constraints ||
      kind == SectionKind::WorkingMemory) {
    return std::nullopt;
  }

  std::lock_guard lock(mutex_);

  // Check capacity: if need to evict, remove oldest files first
  size_t needed = current_total_bytes_ + content.size();
  if (needed > max_total_bytes_) {
    // Collect all spill files sorted by turn index (oldest first)
    std::vector<std::filesystem::path> files;
    for (auto& entry : std::filesystem::directory_iterator(base_dir_)) {
      if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end(),
      [](const std::filesystem::path& a, const std::filesystem::path& b) {
        auto stem_a = a.stem().string();
        auto stem_b = b.stem().string();
        auto us_a = stem_a.find('_');
        auto us_b = stem_b.find('_');
        int ta = 0, tb = 0;
        if (us_a != std::string::npos) {
          try { ta = std::stoi(stem_a.substr(0, us_a)); } catch (...) {}
        }
        if (us_b != std::string::npos) {
          try { tb = std::stoi(stem_b.substr(0, us_b)); } catch (...) {}
        }
        return ta < tb;
      });

    for (auto& f : files) {
      if (current_total_bytes_ + content.size() <= max_total_bytes_) break;
      auto sz = std::filesystem::file_size(f);
      std::filesystem::remove(f);
      current_total_bytes_ -= sz;
      spdlog::debug("SpillStore: evicted {} ({} bytes) to make room", f.string(), sz);
    }

    if (current_total_bytes_ + content.size() > max_total_bytes_) {
      spdlog::warn("SpillStore: cannot fit {} bytes — evicted all files, still over budget",
                   content.size());
      return std::nullopt;
    }
  }

  auto fname = std::to_string(turn_index) + "_"
             + section_kind_name(kind) + ".txt";
  auto path = base_dir_ / fname;

  std::ofstream f(path);
  if (!f) {
    spdlog::warn("SpillStore: failed to open spill file {}", path.string());
    return std::nullopt;
  }
  f << content;
  f.close();

  current_total_bytes_ += content.size();

  std::size_t h = std::hash<std::string>{}(content);
  std::ostringstream hash_oss;
  hash_oss << std::hex << h;

  return SpillReference{kind, path.string(), content.size(), hash_oss.str()};
}

std::optional<std::string> SpillStore::recall(const SpillReference& ref) {
  std::ifstream f(ref.path);
  if (!f) return std::nullopt;
  std::ostringstream oss;
  oss << f.rdbuf();
  return oss.str();
}

void SpillStore::purge_before(int turn_index) {
  std::lock_guard lock(mutex_);
  for (auto& entry : std::filesystem::directory_iterator(base_dir_)) {
    auto stem = entry.path().stem().string();
    auto underscore = stem.find('_');
    if (underscore == std::string::npos) continue;
    try {
      int t = std::stoi(stem.substr(0, underscore));
      if (t < turn_index) {
        auto sz = entry.file_size();
        std::filesystem::remove(entry);
        current_total_bytes_ -= sz;
      }
    } catch (...) {}
  }
}

} // namespace merak
