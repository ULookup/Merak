#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <filesystem>
#include <string>
#include <optional>
#include <mutex>

namespace merak {

class SpillStore {
public:
  explicit SpillStore(const std::filesystem::path& base_dir,
                       size_t max_total_bytes = 100 * 1024 * 1024);

  std::optional<SpillReference> spill(SectionKind kind,
                                        const std::string& content,
                                        int turn_index);

  std::optional<std::string> recall(const SpillReference& ref);

  void purge_before(int turn_index);

private:
  std::filesystem::path base_dir_;
  size_t max_total_bytes_;
  size_t current_total_bytes_ = 0;
  std::mutex mutex_;

  void recompute_total();
};

} // namespace merak
