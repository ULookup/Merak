#pragma once

#include <merak/worldbuilding/world_models.hpp>
#include <merak/worldbuilding/world_store.hpp>
#include <merak/worldbuilding/foreshadowing_store.hpp>
#include <merak/worldbuilding/pg_helpers.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace merak::worldbuilding {

struct KnowledgeView {
    std::string character_id;
    KnowledgeState state = KnowledgeState::Unknown;
    std::string context_snippet;
};

struct LeakRisk {
    std::string secret_id;
    std::string character_id;
    std::string reason;
};

class SecretStore {
public:
    SecretStore(WorldStore& worlds,
                ForeshadowingStore& foreshadowing,
                std::string_view pg_conninfo,
                std::filesystem::path data_root);

    Secret create(const std::string& world_id, Secret secret);
    Secret transfer(const std::string& world_id,
                    const std::string& secret_id,
                    const std::string& character_id);
    Secret expose(const std::string& world_id,
                  const std::string& secret_id,
                  const std::string& scene_id);
    Secret abandon(const std::string& world_id, const std::string& secret_id);
    Secret reverse_truth(const std::string& world_id,
                          const std::string& secret_id,
                          std::string deeper_truth,
                          std::string new_stakes);

    // 部分更新秘密字段 (status, aware_character_ids, suspicious_character_ids, public_version, stakes)
    bool patch(const std::string& world_id, const std::string& id,
               const nlohmann::json& fields);

    std::vector<Secret> list(const std::string& world_id,
                              std::optional<SecretStatus> status) const;
    std::vector<KnowledgeView>
    scene_asymmetry(const std::string& world_id, const Scene& scene) const;
    std::vector<LeakRisk>
    check_leak_risk(const std::string& world_id,
                    const Scene& scene,
                    const std::string& draft_text) const;

private:
    void initialize();

    WorldStore& worlds_;
    ForeshadowingStore& foreshadowing_;
    std::filesystem::path data_root_;
    std::unique_ptr<PgPool> pool_;
};

} // namespace merak::worldbuilding
