#pragma once

#include <merak/worldbuilding/world_models.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace merak::worldbuilding {

class WorldStore {
public:
    explicit WorldStore(std::filesystem::path data_root);

    void initialize();
    WorldMeta create_world(const std::string& name,
                           const std::string& description);
    std::optional<WorldMeta> get_world(const std::string& world_id) const;
    std::vector<WorldMeta> list_worlds() const;
    bool delete_world(const std::string& world_id);

    void add_world_knowledge(const std::string& world_id,
                             WorldKnowledge item);
    std::vector<WorldKnowledge>
    get_world_knowledge(const std::string& world_id,
                        const std::string& category) const;

    std::vector<AgentRecord> list_agents(const std::string& world_id) const;
    std::filesystem::path world_path(const std::string& world_id) const;

private:
    std::filesystem::path database_path() const;

    std::filesystem::path data_root_;
};

} // namespace merak::worldbuilding
