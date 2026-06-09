#include <merak/spill_store.hpp>
#include <fstream>
#include <sstream>
#include <functional>

namespace merak {

SpillStore::SpillStore(const std::filesystem::path& base_dir, size_t max_bytes)
  : base_dir_(base_dir), max_total_bytes_(max_bytes) {
  std::filesystem::create_directories(base_dir_);
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

  auto fname = std::to_string(turn_index) + "_"
             + section_kind_name(kind) + ".txt";
  auto path = base_dir_ / fname;

  std::ofstream f(path);
  if (!f) return std::nullopt;
  f << content;
  f.close();

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
      if (t < turn_index) std::filesystem::remove(entry);
    } catch (...) {}
  }
}

} // namespace merak
