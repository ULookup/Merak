#pragma once

#include <merak/worldbuilding/world_models.hpp>
#include <merak/worldbuilding/world_store.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace merak::worldbuilding {

class PgPool;

class AgentStore {
public:
    AgentStore(WorldStore& worlds, std::string_view pg_conninfo,
               std::filesystem::path data_root);
    ~AgentStore();

    AgentRecord create_manager(const std::string& world_id, AgentKind kind,
                               std::string name, std::string instructions);
    AgentRecord create_character(const std::string& world_id,
                                 CharacterCard card);
    AgentRecord create_group(const std::string& world_id, std::string name,
                             std::string culture_card_markdown,
                             std::vector<std::string> member_agent_ids);

    std::optional<AgentRecord> get_agent(const std::string& agent_id) const;
    std::vector<AgentRecord> list_agents(const std::string& world_id) const;
    bool delete_agent(const std::string& agent_id);

    CharacterCard load_character_card(const std::string& agent_id) const;
    CharacterCard update_character_card(const std::string& agent_id,
                                        CharacterCard next_card,
                                        std::string reason);

    // 部分更新角色卡，非整体替换，带 version 校验
    // 抛出 VersionConflictError 如果版本不匹配
    CharacterCard patch_character_card(const std::string& agent_id,
                                       const nlohmann::json& fields,
                                       int expected_version);

    void append_diary_entry(DiaryEntry entry);
    std::vector<DiaryEntry> recent_diary(const std::string& agent_id,
                                         int max_entries) const;
    std::vector<DiaryEntry> recent_diary_headers(const std::string& agent_id,
                                                  int max_entries) const;
    void write_memory_summary(MemorySummary summary);

    std::vector<DiaryEntry> search_diary(const std::string& agent_id,
                                         const std::string& keyword,
                                         int max_results = 5) const;
    std::optional<DiaryEntry> get_diary(const std::string& diary_id) const;

    void update_diary_content(const std::string& diary_id, const std::string& content,
                              const std::string& mood, int leak_risk_level, int tokens_used);
    std::vector<MemorySummary> recent_summaries(const std::string& agent_id, int limit = 10) const;
    int uncompressed_diary_count(const std::string& agent_id) const;
    std::vector<DiaryEntry> uncompressed_diaries(const std::string& agent_id, int limit) const;

    std::vector<AgentRecord>
    search_agents_by_traits(const std::string& world_id,
                            const std::vector<std::string>& traits,
                            const std::string& identity = "",
                            int max_results = 20) const;

    void update_agent_prompt(const std::string& agent_id, std::string prompt);
    std::string load_agent_prompt(const std::string& agent_id) const;

    void upsert_relation(RelationEntry relation);
    std::vector<RelationEntry> relations_for(const std::string& agent_id) const;

    GroupProfile load_group(const std::string& group_agent_id) const;
    bool can_speak_directly(const std::string& agent_id) const;
    std::vector<std::string>
    shared_memory_refs_for(const std::string& agent_id) const;

private:
    void initialize();
    std::filesystem::path agent_path(const std::string& agent_id) const;
    AgentRecord insert_agent(const std::string& world_id, std::string name,
                             AgentKind kind);

    WorldStore& worlds_;
    std::filesystem::path data_root_;
    std::unique_ptr<PgPool> pool_;
};

} // namespace merak::worldbuilding
