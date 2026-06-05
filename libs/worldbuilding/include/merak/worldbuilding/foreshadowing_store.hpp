#pragma once

#include <merak/worldbuilding/world_models.hpp>
#include <merak/worldbuilding/world_store.hpp>
#include <merak/worldbuilding/narrative_store.hpp>
#include <merak/worldbuilding/pg_helpers.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace merak::worldbuilding {

struct ForeshadowStats {
    int open = 0;
    int paid = 0;
    int abandoned = 0;
};

class ForeshadowingStore {
public:
    ForeshadowingStore(WorldStore& worlds,
                       NarrativeStore& narrative,
                       std::string_view pg_conninfo,
                       std::filesystem::path data_root);

    Foreshadowing plant(const std::string& world_id, Foreshadowing item);
    Foreshadowing pay(const std::string& world_id,
                      const std::string& id,
                      const std::string& scene_id);
    Foreshadowing abandon(const std::string& world_id, const std::string& id);
    std::vector<Foreshadowing>
    list(const std::string& world_id,
         std::optional<ForeshadowStatus> status) const;
    std::vector<Foreshadowing>
    relevant_for_scene(const std::string& world_id, const Scene& scene) const;
    ForeshadowStats stats(const std::string& world_id) const;
    ForeshadowStats chapter_summary(const std::string& world_id,
                                     const std::string& chapter_id) const;
    std::vector<Foreshadowing>
    final_act_reminders(const std::string& world_id) const;

private:
    void initialize();

    WorldStore& worlds_;
    NarrativeStore& narrative_;
    std::filesystem::path data_root_;
    std::unique_ptr<PgPool> pool_;
};

} // namespace merak::worldbuilding
