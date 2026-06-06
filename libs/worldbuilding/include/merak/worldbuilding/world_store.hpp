#pragma once

#include <merak/worldbuilding/world_models.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace merak::worldbuilding {

class PgPool;

class WorldStore {
public:
    WorldStore(std::string_view pg_conninfo, std::filesystem::path data_root);
    ~WorldStore();

    void initialize();
    WorldMeta create_world(const std::string& name,
                           const std::string& description);
    WorldMeta update_world(const std::string& world_id,
                           const std::optional<std::string>& name,
                           const std::optional<std::string>& description);
    std::optional<WorldMeta> get_world(const std::string& world_id) const;
    std::vector<WorldMeta> list_worlds() const;
    bool delete_world(const std::string& world_id);

    void add_world_knowledge(const std::string& world_id,
                             WorldKnowledge item);
    std::vector<WorldKnowledge>
    get_world_knowledge(const std::string& world_id,
                        const std::string& category) const;
    std::vector<WorldKnowledge>
    search_world_knowledge(const std::string& world_id,
                           const std::string& query,
                           const std::string& category = "",
                           int max_results = 20) const;

    std::vector<AgentRecord> list_agents(const std::string& world_id) const;
    std::filesystem::path world_path(const std::string& world_id) const;

private:
    std::filesystem::path data_root_;
    std::unique_ptr<PgPool> pool_;
};

} // namespace merak::worldbuilding
