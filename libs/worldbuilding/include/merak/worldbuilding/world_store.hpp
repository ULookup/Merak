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
    void update_world_config(const std::string& world_id,
                             const nlohmann::json& config);
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

    Location add_location(const std::string& world_id, Location location);
    std::optional<Location> get_location(const std::string& world_id,
                                         const std::string& location_id) const;
    std::vector<Location> list_locations(const std::string& world_id) const;
    bool update_location(const std::string& world_id, const std::string& location_id,
                         const nlohmann::json& fields);
    bool delete_location(const std::string& world_id, const std::string& location_id);

    // Knowledge mutations
    bool update_knowledge(const std::string& world_id, const std::string& knowledge_id,
                          const nlohmann::json& fields);
    bool delete_knowledge(const std::string& world_id, const std::string& knowledge_id);

    // Faction
    Faction add_faction(const std::string& world_id, Faction faction);
    std::optional<Faction> get_faction(const std::string& world_id, const std::string& faction_id) const;
    std::vector<Faction> list_factions(const std::string& world_id) const;
    bool update_faction(const std::string& world_id, const std::string& faction_id,
                        const nlohmann::json& fields);
    bool delete_faction(const std::string& world_id, const std::string& faction_id);

    // Agent operation results
    void store_agent_result(const std::string& world_id, const std::string& operation_type,
                            const nlohmann::json& result);
    std::optional<nlohmann::json> get_agent_result(const std::string& world_id,
                                                    const std::string& operation_type) const;

    // File-world links
    void add_file_link(const std::string& world_id, const std::string& file_path,
                       const std::string& target_type, const std::string& target_id);
    bool remove_file_link(const std::string& world_id, const std::string& file_path,
                          const std::string& target_type, const std::string& target_id);
    nlohmann::json list_file_links(const std::string& world_id) const;

    std::vector<AgentRecord> list_agents(const std::string& world_id) const;
    std::filesystem::path world_path(const std::string& world_id) const;

private:
    std::filesystem::path data_root_;
    std::unique_ptr<PgPool> pool_;
};

} // namespace merak::worldbuilding
