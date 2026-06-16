#include <merak/worldbuilding/worldbuilding_tools.hpp>
#include <merak/worldbuilding/card_access.hpp>
#include <merak/kg/kg_provider.hpp>
#include <nlohmann/json.hpp>
#include <future>
#include <algorithm>
#include <set>
#include <sstream>
#include <merak/llm_provider.hpp>
#include <merak/worldbuilding/extraction_service.hpp>
#include <merak/worldbuilding/ids.hpp>
#include <merak/agent_loop.hpp>
#include <merak/tool_registry.hpp>
#include <merak/token_counter.hpp>
#include <merak/compactor.hpp>
#include <merak/memory_store.hpp>
#include "prompts/writer.hpp"

namespace merak::worldbuilding {

using json = nlohmann::json;

// ========== DescribeCharacterTool ==========

ToolSpec DescribeCharacterTool::spec() const {
    ToolSpec s;
    s.name = "describe_character";
    s.description = R"(Observe a character in the current scene. Returns their public appearance, identity, gender, and age. Only works for characters present in the current scene. Example: describe_character(agent_ailin))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "agent_id": {"type": "string", "description": "Target character's agent_id"}
        },
        "required": ["agent_id"]
    })";
    return s;
}

ToolMeta DescribeCharacterTool::meta() const {
    ToolMeta m;
    m.name = "describe_character";
    m.description = "Get detailed character profile by name";
    m.triggers = {"character", "describe", "profile"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> DescribeCharacterTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string target_id = args.value("agent_id", "");

            if (target_id.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "describe_character 需要传入角色 ID。例如：describe_character(agent_ailin)");
                return result;
            }

            auto& svc = *static_cast<DescribeCharacterTool&>(*self).svc_;


            auto agent_opt = svc.agents().get_agent(target_id);
            if (!agent_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + target_id + "' 不存在于世界中。");
                return result;
            }

            auto scene_opt = svc.narrative().get_scene(exec_ctx.world_id, exec_ctx.scene_id);
            if (!scene_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "当前场景未初始化。");
                return result;
            }

            const auto& participants = scene_opt->participant_ids;
            if (!is_in_vector(participants, target_id)) {
                std::string names;
                for (auto& pid : participants) {
                    auto ag = svc.agents().get_agent(pid);
                    if (ag) names += (names.empty() ? "" : "、") + ag->name;
                }
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    agent_opt->name + " 不在当前场景中。当前在场角色：" + names + "。");
                return result;
            }

            auto card = svc.agents().load_character_card(target_id);
            json data{
                {"appearance", card.appearance},
                {"identity", card.identity},
                {"gender", card.gender},
                {"age", card.age}
            };
            result.output = ok_response(data);

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("describe_character 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== SearchMyDiaryTool ==========

ToolSpec SearchMyDiaryTool::spec() const {
    ToolSpec s;
    s.name = "search_my_diary";
    s.description = R"(Search your own diary entries by keyword. Returns up to 5 matching entries with scene_name, world_time, and a content snippet (max 100 chars). Query must be at least 2 characters. Example: search_my_diary(童年))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "query": {"type": "string", "description": "Search keyword, at least 2 characters"}
        },
        "required": ["query"]
    })";
    return s;
}

ToolMeta SearchMyDiaryTool::meta() const {
    ToolMeta m;
    m.name = "search_my_diary";
    m.description = "Search your own diary entries by keyword";
    m.triggers = {"diary", "memory", "search diary"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> SearchMyDiaryTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string query = args.value("query", "");

            if (query.size() < 2) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "搜索词不能为空或单字，请使用至少 2 个字的查询。");
                return result;
            }

            auto& svc = *static_cast<SearchMyDiaryTool&>(*self).svc_;

            auto entries = svc.agents().search_diary(exec_ctx.caller_agent_id, query, 5);
            if (entries.empty()) {
                result.output = error_response(ToolErrorCode::EMPTY_RESULT,
                    "在你的日记中没有找到与 '" + query + "' 相关的内容。尝试更宽泛的词语。");
                return result;
            }

            json arr = json::array();
            for (auto& e : entries) {
                arr.push_back({
                    {"scene_name", e.scene_id},
                    {"world_time", e.world_time},
                    {"content_snippet", make_snippet(e.content, 100)}
                });
            }
            result.output = ok_response({{"entries", arr}});

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("search_my_diary 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== LookAroundTool ==========

ToolSpec LookAroundTool::spec() const {
    ToolSpec s;
    s.name = "look_around";
    s.description = R"(Observe your current surroundings. Returns the location name, description, list of characters present, and current world time. Takes no parameters.)";
    s.source = "builtin";
    s.parameters_json = R"({"type": "object", "properties": {}, "required": []})";
    return s;
}

ToolMeta LookAroundTool::meta() const {
    ToolMeta m;
    m.name = "look_around";
    m.description = "Observe current surroundings and characters present";
    m.triggers = {"look", "observe", "surroundings", "where am i"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> LookAroundTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto& svc = *static_cast<LookAroundTool&>(*self).svc_;

            auto scene_opt = svc.narrative().get_scene(exec_ctx.world_id, exec_ctx.scene_id);
            if (!scene_opt) {
                result.is_error = true;
                result.output = error_response(ToolErrorCode::INTERNAL,
                    "无法读取当前场景信息。当前场景可能未初始化。");
                return result;
            }

            std::vector<std::string> names;
            for (auto& pid : scene_opt->participant_ids) {
                auto ag = svc.agents().get_agent(pid);
                if (ag) names.push_back(ag->name);
            }

            json data{
                {"location_name", scene_opt->title},
                {"location_description", scene_opt->narrative},
                {"in_scene_characters", names},
                {"world_time", scene_opt->world_time}
            };
            result.output = ok_response(data);

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("look_around 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== QueryMapTool ==========

ToolSpec QueryMapTool::spec() const {
    ToolSpec s;
    s.name = "query_map";
    s.description = R"(Query geographic data for a region. Returns terrain, towns, borders, and known locations. Example: query_map(北境))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "region": {"type": "string", "description": "Region name to query"}
        },
        "required": ["region"]
    })";
    return s;
}

ToolMeta QueryMapTool::meta() const {
    ToolMeta m;
    m.name = "query_map";
    m.description = "Query geographic map data for a region";
    m.triggers = {"geography", "map", "region", "terrain"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> QueryMapTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string region = args.value("region", "");

            if (region.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "请指定要查询的区域名称，例如：query_map(北境)");
                return result;
            }

            auto& svc = *static_cast<QueryMapTool&>(*self).svc_;

            auto results = svc.worlds().search_world_knowledge(exec_ctx.world_id, region, "map", 1);
            if (!results.empty()) {
                result.output = ok_response({{"region", region}, {"data", results[0].content}});
                return result;
            }

            auto knowledge = svc.worlds().get_world_knowledge(exec_ctx.world_id, "map");
            std::string known;
            for (auto& kw : knowledge) known += (known.empty() ? "" : "、") + kw.content;

            result.output = error_response(ToolErrorCode::EMPTY_RESULT,
                "地图中没有关于 '" + region + "' 的记录。" +
                (known.empty() ? "" : "已知区域：" + known + "。"));

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("query_map 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== QueryHistoryTool ==========

ToolSpec QueryHistoryTool::spec() const {
    ToolSpec s;
    s.name = "query_history";
    s.description = R"(Query historical timeline events by keyword or time range. Returns matching TimelineEvent entries with world_time, description, and related character IDs. Example: query_history(狼烟))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "keyword": {"type": "string", "description": "Keyword or time range to search"}
        },
        "required": ["keyword"]
    })";
    return s;
}

ToolMeta QueryHistoryTool::meta() const {
    ToolMeta m;
    m.name = "query_history";
    m.description = "Query historical timeline events by keyword";
    m.triggers = {"events", "history", "past", "timeline"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> QueryHistoryTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string keyword = args.value("keyword", "");

            if (keyword.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "请提供查询关键词，例如：query_history(狼烟)");
                return result;
            }

            auto& svc = *static_cast<QueryHistoryTool&>(*self).svc_;

            auto knowledge = svc.worlds().search_world_knowledge(exec_ctx.world_id, keyword, "history");
            json arr = json::array();
            for (auto& kw : knowledge) {
                arr.push_back({
                    {"world_time", kw.created_at},
                    {"description", kw.content}
                });
            }

            if (arr.empty()) {
                std::string hint;
                auto all_history = svc.worlds().get_world_knowledge(exec_ctx.world_id, "history");
                if (!all_history.empty()) {
                    hint = "最近事件：" + make_snippet(all_history.back().content, 80);
                }
                result.output = error_response(ToolErrorCode::EMPTY_RESULT,
                    "历史记录中没有匹配 '" + keyword + "' 的事件。" + hint);
            } else {
                result.output = ok_response({{"events", arr}});
            }

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("query_history 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== QueryMagicTool ==========

ToolSpec QueryMagicTool::spec() const {
    ToolSpec s;
    s.name = "query_magic";
    s.description = R"(Query magic system rules by topic. Returns matching magic rule explanations. Example: query_magic(狼烟令激活条件))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "topic": {"type": "string", "description": "Magic topic to query"}
        },
        "required": ["topic"]
    })";
    return s;
}

ToolMeta QueryMagicTool::meta() const {
    ToolMeta m;
    m.name = "query_magic";
    m.description = "Query magic system rules by topic";
    m.triggers = {"arcane", "enchantment", "magic", "spell"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> QueryMagicTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string topic = args.value("topic", "");

            if (topic.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "请指定查询主题，例如：query_magic(狼烟令激活条件)");
                return result;
            }

            auto& svc = *static_cast<QueryMagicTool&>(*self).svc_;

            auto results = svc.worlds().search_world_knowledge(exec_ctx.world_id, topic, "magic", 1);
            if (!results.empty()) {
                result.output = ok_response({{"topic", topic}, {"rule", results[0].content}});
                return result;
            }

            result.output = error_response(ToolErrorCode::EMPTY_RESULT,
                "魔法规则中尚未定义关于 '" + topic + "' 的内容。");

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("query_magic 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== QueryFactionTool ==========

ToolSpec QueryFactionTool::spec() const {
    ToolSpec s;
    s.name = "query_faction";
    s.description = R"(Query faction information by name. Returns members, alliances, rivalries, and recent events. Example: query_faction(北方蛮族))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "name": {"type": "string", "description": "Faction name to query"}
        },
        "required": ["name"]
    })";
    return s;
}

ToolMeta QueryFactionTool::meta() const {
    ToolMeta m;
    m.name = "query_faction";
    m.description = "Query faction information including members and rivalries";
    m.triggers = {"clan", "faction", "group", "guild"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> QueryFactionTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string name = args.value("name", "");

            if (name.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "请指定势力名称，例如：query_faction(北方蛮族)");
                return result;
            }

            auto& svc = *static_cast<QueryFactionTool&>(*self).svc_;

            auto results = svc.worlds().search_world_knowledge(exec_ctx.world_id, name, "faction", 1);
            if (!results.empty()) {
                result.output = ok_response({{"name", name}, {"info", results[0].content}});
                return result;
            }

            auto knowledge = svc.worlds().get_world_knowledge(exec_ctx.world_id, "faction");
            std::string known;
            for (auto& kw : knowledge) known += (known.empty() ? "" : "、") + kw.content;
            result.output = error_response(ToolErrorCode::EMPTY_RESULT,
                "势力 '" + name + "' 未在系统中注册。" +
                (known.empty() ? "" : "已知势力：" + known + "。"));

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("query_faction 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== ReadCharacterCardTool ==========

ToolSpec ReadCharacterCardTool::spec() const {
    ToolSpec s;
    s.name = "read_character_card";
    s.description = R"(Read the complete CharacterCard of a character. Returns all fields including personality traits, core desire, deep fear, taboo topics, relations, and voice fingerprint. Only works for Individual agents, not managers or groups. Example: read_character_card(agent_kalun))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "agent_id": {"type": "string", "description": "Character's agent_id"}
        },
        "required": ["agent_id"]
    })";
    return s;
}

ToolMeta ReadCharacterCardTool::meta() const {
    ToolMeta m;
    m.name = "read_character_card";
    m.description = "Read the complete character card with all traits and background";
    m.triggers = {"character card", "profile", "read card", "traits"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> ReadCharacterCardTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string agent_id = args.value("agent_id", "");

            auto& svc = *static_cast<ReadCharacterCardTool&>(*self).svc_;

            auto agent_opt = svc.agents().get_agent(agent_id);
            if (!agent_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + agent_id + "' 不存在。");
                return result;
            }

            if (agent_opt->kind != AgentKind::Individual) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "'" + agent_id + "' 是" + to_string(agent_opt->kind) + "，没有角色卡。");
                return result;
            }

            auto card = svc.agents().load_character_card(agent_id);
            json data{
                {"agent_id", card.agent_id},
                {"name", card.name},
                {"gender", card.gender},
                {"race", card.race},
                {"age", card.age},
                {"identity", card.identity},
                {"appearance", card.appearance},
                {"emotional_tendency", card.emotional_tendency},
                {"speaking_style", card.speaking_style},
                {"core_desire", card.core_desire},
                {"deep_fear", card.deep_fear},
                {"daily_goal", card.daily_goal},
                {"background", card.background},
                {"knowledge_scope", card.knowledge_scope},
                {"core_traits", card.core_traits},
                {"taboo_topics", card.taboo_topics},
                {"relations", card.relations},
                {"version", card.version}
            };
            result.output = ok_response(data);

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("read_character_card 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== Helpers for lookup ==========

static std::optional<Secret> find_secret_by_id(SecretStore& store, const std::string& world_id, const std::string& secret_id) {
    for (auto status : {SecretStatus::Active, SecretStatus::Exposed, SecretStatus::Abandoned}) {
        auto list = store.list(world_id, status);
        for (auto& s : list) {
            if (s.id == secret_id) return s;
        }
    }
    return std::nullopt;
}

static std::optional<Foreshadowing> find_foreshadowing_by_id(ForeshadowingStore& store, const std::string& world_id, const std::string& id) {
    for (auto status : {ForeshadowStatus::Open, ForeshadowStatus::Paid, ForeshadowStatus::Abandoned}) {
        auto list = store.list(world_id, status);
        for (auto& f : list) {
            if (f.id == id) return f;
        }
    }
    return std::nullopt;
}

// ========== ReadSecretTool ==========

ToolSpec ReadSecretTool::spec() const {
    ToolSpec s;
    s.name = "read_secret";
    s.description = R"(Read the full details of a secret by its ID. Returns truth, public_version, holder, aware/suspicious characters, believed_truths, and status. Example: read_secret(secret_old_pact))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "secret_id": {"type": "string", "description": "Secret ID to read"}
        },
        "required": ["secret_id"]
    })";
    return s;
}

ToolMeta ReadSecretTool::meta() const {
    ToolMeta m;
    m.name = "read_secret";
    m.description = "Read the full details of a secret by its ID";
    m.triggers = {"confidential", "read secret", "secret"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> ReadSecretTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string secret_id = args.value("secret_id", "");

            auto& svc = *static_cast<ReadSecretTool&>(*self).svc_;

            auto secret_opt = find_secret_by_id(svc.secrets(), exec_ctx.world_id, secret_id);
            if (!secret_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "秘密 '" + secret_id + "' 不存在。");
                return result;
            }

            if (secret_opt->status == SecretStatus::Abandoned) {
                result.output = error_response(ToolErrorCode::CONFLICT,
                    "秘密 '" + secret_id + "' 已废弃（abandoned）。如需查看，请使用 list_secrets(status=abandoned)。");
                return result;
            }

            json data{
                {"secret_id", secret_opt->id},
                {"truth", secret_opt->truth},
                {"public_version", secret_opt->public_version},
                {"holder_id", secret_opt->holder_id},
                {"aware_character_ids", secret_opt->aware_character_ids},
                {"suspicious_character_ids", secret_opt->suspicious_character_ids},
                {"believed_truths", secret_opt->believed_truths},
                {"stakes", secret_opt->stakes},
                {"status", to_string(secret_opt->status)},
                {"related_foreshadowing_ids", secret_opt->related_foreshadowing_ids}
            };
            result.output = ok_response(data);

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("read_secret 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== ReadForeshadowingTool ==========

ToolSpec ReadForeshadowingTool::spec() const {
    ToolSpec s;
    s.name = "read_foreshadowing";
    s.description = R"(Read full details of a foreshadowing item by ID. Returns content, pay_off_idea, hint_level, tags, related secrets, status, and creator. Example: read_foreshadowing(foreshadow_smith_finger))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "foreshadowing_id": {"type": "string", "description": "Foreshadowing ID to read"}
        },
        "required": ["foreshadowing_id"]
    })";
    return s;
}

ToolMeta ReadForeshadowingTool::meta() const {
    ToolMeta m;
    m.name = "read_foreshadowing";
    m.description = "Read full details of a foreshadowing item";
    m.triggers = {"foreshadow", "hint", "read foreshadow"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> ReadForeshadowingTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string foreshadowing_id = args.value("foreshadowing_id", "");

            auto& svc = *static_cast<ReadForeshadowingTool&>(*self).svc_;

            auto f_opt = find_foreshadowing_by_id(svc.foreshadowing(), exec_ctx.world_id, foreshadowing_id);
            if (!f_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "伏笔 '" + foreshadowing_id + "' 不存在。");
                return result;
            }

            json data{
                {"foreshadowing_id", f_opt->id},
                {"content", f_opt->content},
                {"pay_off_idea", f_opt->pay_off_idea},
                {"hint_level", to_string(f_opt->hint_level)},
                {"tags", f_opt->tags},
                {"related_secret_ids", f_opt->related_secret_ids},
                {"related_foreshadowing_ids", f_opt->related_foreshadowing_ids},
                {"status", to_string(f_opt->status)},
                {"created_by", to_string(f_opt->created_by)}
            };
            result.output = ok_response(data);

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("read_foreshadowing 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== ListOpenForeshadowingTool ==========

ToolSpec ListOpenForeshadowingTool::spec() const {
    ToolSpec s;
    s.name = "list_open_foreshadowing";
    s.description = R"(List all currently open (unpaid) foreshadowing items in the world. Takes no parameters.)";
    s.source = "builtin";
    s.parameters_json = R"({"type": "object", "properties": {}, "required": []})";
    return s;
}

ToolMeta ListOpenForeshadowingTool::meta() const {
    ToolMeta m;
    m.name = "list_open_foreshadowing";
    m.description = "List all currently open foreshadowing items";
    m.triggers = {"foreshadow", "list foreshadow", "open foreshadow"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> ListOpenForeshadowingTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto& svc = *static_cast<ListOpenForeshadowingTool&>(*self).svc_;

            auto items = svc.foreshadowing().list(exec_ctx.world_id, ForeshadowStatus::Open);
            if (items.empty()) {
                result.output = error_response(ToolErrorCode::EMPTY_RESULT,
                    "当前世界没有未偿还的伏笔。");
                return result;
            }

            json arr = json::array();
            for (auto& f : items) {
                arr.push_back({
                    {"foreshadowing_id", f.id},
                    {"content", f.content},
                    {"pay_off_idea", f.pay_off_idea},
                    {"hint_level", to_string(f.hint_level)},
                    {"tags", f.tags}
                });
            }
            result.output = ok_response({{"open_foreshadowing", arr}, {"count", items.size()}});

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("list_open_foreshadowing 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== AdvanceWorldTimeTool ==========

ToolSpec AdvanceWorldTimeTool::spec() const {
    ToolSpec s;
    s.name = "advance_world_time";
    s.description = R"(Advance the current world time by a delta. Accepts formats like "2h", "1d", or named times like "第四日午". Time can only move forward. Side-effect: writes a TimelineEvent. Example: advance_world_time(2h) or advance_world_time(第四日午))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "delta": {"type": "string", "description": "Time delta or absolute time label"}
        },
        "required": ["delta"]
    })";
    return s;
}

ToolMeta AdvanceWorldTimeTool::meta() const {
    ToolMeta m;
    m.name = "advance_world_time";
    m.description = "Advance the current world time forward";
    m.triggers = {"advance", "forward", "skip time", "time"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> AdvanceWorldTimeTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string delta = args.value("delta", "");

            if (delta.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "无法解析空时间增量。请使用明确格式，例如：advance_world_time(2h)");
                return result;
            }

            // Validate time format using WorldTime parser
            auto parsed_delta = WorldTime::parse(delta);
            if (!parsed_delta.has_value()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "无法解析时间格式 '" + delta + "'。请使用时间增量（如 2h、1d）、中文时间标签（如 第四日午）或英文格式（如 day3_night）。");
                return result;
            }

            auto& svc = *static_cast<AdvanceWorldTimeTool&>(*self).svc_;

            // Backward time travel check against current scene time
            {
                auto scene_opt = svc.narrative().get_scene(exec_ctx.world_id, exec_ctx.scene_id);
                if (scene_opt && !scene_opt->world_time.empty()) {
                    auto current_time = WorldTime::parse(scene_opt->world_time);
                    if (current_time.has_value() && *parsed_delta <= *current_time) {
                        result.output = error_response(ToolErrorCode::CONFLICT,
                            "时间只能前进，不能倒退。当前场景时间：" + scene_opt->world_time +
                            "，目标时间：" + delta + "。");
                        return result;
                    }
                }
            }

            TimelineEvent ev;
            ev.world_time = delta;
            ev.description = "World time advanced by: " + delta;
            ev.recorded_by = "god_agent";
            auto recorded = svc.narrative().record_timeline_event(exec_ctx.world_id, std::move(ev));

            json data{
                {"new_time", recorded.world_time},
                {"event_id", recorded.id}
            };
            result.output = ok_response(data);

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("advance_world_time 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== CreateCharacterTool ==========

ToolSpec CreateCharacterTool::spec() const {
    ToolSpec s;
    s.name = "create_character";
    s.description = R"(Create a new character in the world. Required: name, gender, age, identity, speaking_style, core_desire, deep_fear, appearance, background, knowledge_scope, core_traits. Returns the new agent_id. Example: create_character(name=卡伦, gender=男, age=35, identity=铁匠, ...))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "name": {"type": "string"},
            "gender": {"type": "string"},
            "age": {"type": "integer"},
            "identity": {"type": "string"},
            "speaking_style": {"type": "string"},
            "core_desire": {"type": "string"},
            "deep_fear": {"type": "string"},
            "appearance": {"type": "string"},
            "background": {"type": "string"},
            "knowledge_scope": {"type": "string"},
            "core_traits": {"type": "array", "items": {"type": "string"}}
        },
        "required": ["name", "identity"]
    })";
    return s;
}

ToolMeta CreateCharacterTool::meta() const {
    ToolMeta m;
    m.name = "create_character";
    m.description = "Create a new character in the world";
    m.triggers = {"add character", "create character", "new character"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> CreateCharacterTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string name = args.value("name", "");
            std::string identity = args.value("identity", "");

            if (name.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "创建角色失败。缺少必填字段：name。");
                return result;
            }
            if (identity.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "创建角色失败。缺少必填字段：identity。");
                return result;
            }

            auto& svc = *static_cast<CreateCharacterTool&>(*self).svc_;

            auto existing = svc.agents().list_agents(exec_ctx.world_id);
            for (auto& ag : existing) {
                if (ag.name == name) {
                    result.output = error_response(ToolErrorCode::CONFLICT,
                        "世界 '" + exec_ctx.world_id + "' 中已存在名为 '" + name + "' 的角色。");
                    return result;
                }
            }

            CharacterCard card;
            card.name = name;
            card.gender = args.value("gender", "");
            card.age = args.value("age", 0);
            card.identity = identity;
            card.speaking_style = args.value("speaking_style", "");
            card.core_desire = args.value("core_desire", "");
            card.deep_fear = args.value("deep_fear", "");
            card.appearance = args.value("appearance", "");
            card.background = args.value("background", "");
            card.knowledge_scope = args.value("knowledge_scope", "");

            if (args.contains("core_traits") && args["core_traits"].is_array()) {
                for (auto& t : args["core_traits"]) {
                    card.core_traits.push_back(t.get<std::string>());
                }
            }

            auto record = svc.agents().create_character(exec_ctx.world_id, std::move(card));
            result.output = ok_response({{"agent_id", record.id}, {"name", record.name}});

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("create_character 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== CreateSceneTool ==========

ToolSpec CreateSceneTool::spec() const {
    ToolSpec s;
    s.name = "create_scene";
    s.description = R"(Create a new scene in the current world. Required: title, chapter_id. Optional: world_time, narrative, participant_ids, location_id, section_id. Returns a creation preview for confirmation.)";
    s.source = "builtin";
    s.requires_confirmation = true;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "title": {"type": "string"},
            "chapter_id": {"type": "string"},
            "world_time": {"type": "string"},
            "narrative": {"type": "string"},
            "participant_ids": {"type": "array", "items": {"type": "string"}},
            "location_id": {"type": "string"},
            "section_id": {"type": "string"}
        },
        "required": ["title", "chapter_id"]
    })";
    return s;
}

ToolMeta CreateSceneTool::meta() const {
    ToolMeta m;
    m.name = "create_scene";
    m.description = "Create a new scene in the current world";
    m.triggers = {"add scene", "create scene", "new scene"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> CreateSceneTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string title = args.value("title", "");
            std::string chapter_id = args.value("chapter_id", "");

            if (title.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "创建场景失败。缺少必填字段：title。");
                return result;
            }
            if (chapter_id.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "创建场景失败。缺少必填字段：chapter_id。");
                return result;
            }

            auto& svc = *static_cast<CreateSceneTool&>(*self).svc_;

            auto preview = svc.build_scene_preview(exec_ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(exec_ctx.world_id, "create_scene", args, preview);

            json output;
            output["ok"] = true;
            output["status"] = "pending_creation";
            output["creation_id"] = creation_id;
            output["tool"] = "create_scene";
            output["preview"] = preview;
            result.output = output.dump();

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("create_scene 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== CreateChapterTool ==========

ToolSpec CreateChapterTool::spec() const {
    ToolSpec s;
    s.name = "create_chapter";
    s.description = R"(Create a new chapter/act in the current world. Required: title. Optional: arc_id, number (order index), pitch (summary). Returns a creation preview for confirmation.)";
    s.source = "builtin";
    s.requires_confirmation = true;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "title": {"type": "string"},
            "arc_id": {"type": "string"},
            "number": {"type": "integer"},
            "order_index": {"type": "integer"},
            "pitch": {"type": "string"},
            "summary": {"type": "string"}
        },
        "required": ["title"]
    })";
    return s;
}

ToolMeta CreateChapterTool::meta() const {
    ToolMeta m;
    m.name = "create_chapter";
    m.description = "Create a new chapter in the current world";
    m.triggers = {"add chapter", "create chapter", "new chapter"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> CreateChapterTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string title = args.value("title", "");

            if (title.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "创建章节失败。缺少必填字段：title。");
                return result;
            }

            auto& svc = *static_cast<CreateChapterTool&>(*self).svc_;

            auto preview = svc.build_chapter_preview(exec_ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(exec_ctx.world_id, "create_chapter", args, preview);

            json output;
            output["ok"] = true;
            output["status"] = "pending_creation";
            output["creation_id"] = creation_id;
            output["tool"] = "create_chapter";
            output["preview"] = preview;
            result.output = output.dump();

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("create_chapter 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== CreateArcTool ==========

ToolSpec CreateArcTool::spec() const {
    ToolSpec s;
    s.name = "create_arc";
    s.description = R"(Create a new story arc in the current world. Required: title (also accepts name). Optional: purpose/description, chapter_numbers/chapter_ids. Returns a creation preview for confirmation.)";
    s.source = "builtin";
    s.requires_confirmation = true;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "title": {"type": "string"},
            "name": {"type": "string"},
            "purpose": {"type": "string"},
            "description": {"type": "string"},
            "chapter_numbers": {"type": "array", "items": {"type": "integer"}},
            "chapter_ids": {"type": "array", "items": {"type": "string"}}
        },
        "required": ["title"]
    })";
    return s;
}

ToolMeta CreateArcTool::meta() const {
    ToolMeta m;
    m.name = "create_arc";
    m.description = "Create a new story arc in the current world";
    m.triggers = {"add arc", "create arc", "new arc", "story arc"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> CreateArcTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string title = args.value("title", "");
            if (title.empty()) title = args.value("name", "");

            if (title.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "创建弧线失败。缺少必填字段：title。");
                return result;
            }

            auto& svc = *static_cast<CreateArcTool&>(*self).svc_;

            auto preview = svc.build_arc_preview(exec_ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(exec_ctx.world_id, "create_arc", args, preview);

            json output;
            output["ok"] = true;
            output["status"] = "pending_creation";
            output["creation_id"] = creation_id;
            output["tool"] = "create_arc";
            output["preview"] = preview;
            result.output = output.dump();

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("create_arc 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== CreateSecretTool ==========

ToolSpec CreateSecretTool::spec() const {
    ToolSpec s;
    s.name = "create_secret";
    s.description = R"(Create a new secret in the current world. Required: truth (also accepts content). Optional: holder_id/holder_agent_ids, stakes. Returns a creation preview for confirmation.)";
    s.source = "builtin";
    s.requires_confirmation = true;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "truth": {"type": "string"},
            "content": {"type": "string"},
            "holder_id": {"type": "string"},
            "holder_agent_ids": {"type": "array", "items": {"type": "string"}},
            "stakes": {"type": "string"}
        },
        "required": ["truth"]
    })";
    return s;
}

ToolMeta CreateSecretTool::meta() const {
    ToolMeta m;
    m.name = "create_secret";
    m.description = "Create a new secret in the current world";
    m.triggers = {"add secret", "create secret", "new secret"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> CreateSecretTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string truth = args.value("truth", "");
            if (truth.empty()) truth = args.value("content", "");

            if (truth.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "创建秘密失败。缺少必填字段：truth。");
                return result;
            }

            auto& svc = *static_cast<CreateSecretTool&>(*self).svc_;

            auto preview = svc.build_secret_preview(exec_ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(exec_ctx.world_id, "create_secret", args, preview);

            json output;
            output["ok"] = true;
            output["status"] = "pending_creation";
            output["creation_id"] = creation_id;
            output["tool"] = "create_secret";
            output["preview"] = preview;
            result.output = output.dump();

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("create_secret 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== AddWorldKnowledgeTool ==========

ToolSpec AddWorldKnowledgeTool::spec() const {
    ToolSpec s;
    s.name = "add_world_knowledge";
    s.description = R"(Add a new world knowledge entry. Required: category, content. Optional: tags, related_ids/related_entity_ids. Returns a creation preview for confirmation.)";
    s.source = "builtin";
    s.requires_confirmation = true;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "category": {"type": "string"},
            "content": {"type": "string"},
            "tags": {"type": "array", "items": {"type": "string"}},
            "related_ids": {"type": "array", "items": {"type": "string"}},
            "related_entity_ids": {"type": "array", "items": {"type": "string"}}
        },
        "required": ["category", "content"]
    })";
    return s;
}

ToolMeta AddWorldKnowledgeTool::meta() const {
    ToolMeta m;
    m.name = "add_world_knowledge";
    m.description = "Add a new world knowledge entry";
    m.triggers = {"add knowledge", "knowledge", "world knowledge"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> AddWorldKnowledgeTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string category = args.value("category", "");
            std::string content = args.value("content", "");

            if (category.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "添加世界知识失败。缺少必填字段：category。");
                return result;
            }
            if (content.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "添加世界知识失败。缺少必填字段：content。");
                return result;
            }

            auto& svc = *static_cast<AddWorldKnowledgeTool&>(*self).svc_;

            auto preview = svc.build_world_knowledge_preview(exec_ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(exec_ctx.world_id, "add_world_knowledge", args, preview);

            json output;
            output["ok"] = true;
            output["status"] = "pending_creation";
            output["creation_id"] = creation_id;
            output["tool"] = "add_world_knowledge";
            output["preview"] = preview;
            result.output = output.dump();

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("add_world_knowledge 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== CreateLocationTool ==========

ToolSpec CreateLocationTool::spec() const {
    ToolSpec s;
    s.name = "create_location";
    s.description = R"(Create a new location in the current world. Required: name. Optional: description, region, parent_location_id. Returns a creation preview for confirmation.)";
    s.source = "builtin";
    s.requires_confirmation = true;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "name": {"type": "string"},
            "description": {"type": "string"},
            "region": {"type": "string"},
            "parent_location_id": {"type": "string"}
        },
        "required": ["name"]
    })";
    return s;
}

ToolMeta CreateLocationTool::meta() const {
    ToolMeta m;
    m.name = "create_location";
    m.description = "Create a new location in the current world";
    m.triggers = {"add location", "add place", "create location", "new location"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> CreateLocationTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string name = args.value("name", "");

            if (name.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "创建地点失败。缺少必填字段：name。");
                return result;
            }

            auto& svc = *static_cast<CreateLocationTool&>(*self).svc_;

            auto preview = svc.build_location_preview(exec_ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(exec_ctx.world_id, "create_location", args, preview);

            json output;
            output["ok"] = true;
            output["status"] = "pending_creation";
            output["creation_id"] = creation_id;
            output["tool"] = "create_location";
            output["preview"] = preview;
            result.output = output.dump();

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("create_location 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== PlantForeshadowingTool ==========

ToolSpec PlantForeshadowingTool::spec() const {
    ToolSpec s;
    s.name = "plant_foreshadowing";
    s.description = R"(Plant a new foreshadowing item. hint_level must be one of: subtle, visible, obvious. Example: plant_foreshadowing(content=铁匠的手在颤抖, pay_off_idea=旧伤复发影响决战, hint_level=subtle, tags=["铁匠","健康"]))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "content": {"type": "string", "description": "The foreshadowing content"},
            "pay_off_idea": {"type": "string", "description": "How this should pay off"},
            "hint_level": {"type": "string", "enum": ["subtle", "visible", "obvious"]},
            "tags": {"type": "array", "items": {"type": "string"}}
        },
        "required": ["content", "pay_off_idea", "hint_level"]
    })";
    return s;
}

ToolMeta PlantForeshadowingTool::meta() const {
    ToolMeta m;
    m.name = "plant_foreshadowing";
    m.description = "Plant a new foreshadowing item for future payoff";
    m.triggers = {"foreshadow", "hint", "plant", "setup"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> PlantForeshadowingTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string content = args.value("content", "");

            if (content.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "无法创建空内容的伏笔。请在 content 中描述伏笔内容。");
                return result;
            }

            std::string hint_level_str = args.value("hint_level", "");
            if (hint_level_str != "subtle" && hint_level_str != "visible" && hint_level_str != "obvious") {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "'" + hint_level_str + "' 不是有效的提示等级。可选值：subtle / visible / obvious。");
                return result;
            }

            auto& svc = *static_cast<PlantForeshadowingTool&>(*self).svc_;

            Foreshadowing f;
            f.content = content;
            f.pay_off_idea = args.value("pay_off_idea", "");
            f.status = ForeshadowStatus::Open;
            f.created_by = ForeshadowCreatedBy::Author;

            if (hint_level_str == "subtle") f.hint_level = ForeshadowHintLevel::Subtle;
            else if (hint_level_str == "obvious") f.hint_level = ForeshadowHintLevel::Obvious;
            else f.hint_level = ForeshadowHintLevel::Visible;

            if (args.contains("tags") && args["tags"].is_array()) {
                for (auto& t : args["tags"]) f.tags.push_back(t.get<std::string>());
            }

            auto planted = svc.foreshadowing().plant(exec_ctx.world_id, std::move(f));
            result.output = ok_response({{"foreshadowing_id", planted.id}});

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("plant_foreshadowing 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== ExposeSecretTool ==========

ToolSpec ExposeSecretTool::spec() const {
    ToolSpec s;
    s.name = "expose_secret";
    s.description = R"(Expose a secret in the current scene. Changes secret status to exposed. Automatically pays off related foreshadowing items (best-effort). Example: expose_secret(secret_identity))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "secret_id": {"type": "string", "description": "Secret ID to expose"}
        },
        "required": ["secret_id"]
    })";
    return s;
}

ToolMeta ExposeSecretTool::meta() const {
    ToolMeta m;
    m.name = "expose_secret";
    m.description = "Expose a secret in the current scene";
    m.triggers = {"expose", "reveal", "secret", "uncover"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> ExposeSecretTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string secret_id = args.value("secret_id", "");

            auto& svc = *static_cast<ExposeSecretTool&>(*self).svc_;

            auto secret_opt = find_secret_by_id(svc.secrets(), exec_ctx.world_id, secret_id);
            if (!secret_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "秘密 '" + secret_id + "' 不存在。");
                return result;
            }

            if (secret_opt->status == SecretStatus::Exposed) {
                result.output = error_response(ToolErrorCode::CONFLICT,
                    "秘密 '" + secret_id + "' 已经暴露。秘密只能暴露一次。");
                return result;
            }

            if (secret_opt->status == SecretStatus::Abandoned) {
                result.output = error_response(ToolErrorCode::CONFLICT,
                    "秘密 '" + secret_id + "' 已废弃，无法暴露。如需恢复并暴露，请先联系作者。");
                return result;
            }

            auto exposed = svc.secrets().expose(exec_ctx.world_id, secret_id, exec_ctx.scene_id);

            json warnings = json::array();
            int repaid = 0;
            for (auto& f_id : exposed.related_foreshadowing_ids) {
                try {
                    svc.foreshadowing().pay(exec_ctx.world_id, f_id, exec_ctx.scene_id);
                    repaid++;
                } catch (...) {
                    warnings.push_back({
                        {"code", "NOT_FOUND"},
                        {"message", "关联伏笔 '" + f_id + "' 偿还失败：该伏笔不存在或已删除"}
                    });
                }
            }

            json data{
                {"secret_id", exposed.id},
                {"status", to_string(exposed.status)},
                {"exposed_at", exposed.exposed_at.value_or("")},
                {"related_foreshadowing_repaid", repaid}
            };

            if (warnings.empty()) {
                result.output = ok_response(data);
            } else {
                result.output = partial_success(data, warnings);
            }

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("expose_secret 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== EndSceneTool ==========

ToolSpec EndSceneTool::spec() const {
    ToolSpec s;
    s.name = "end_scene";
    s.description = R"(End the current scene with its final draft text. Writes diaries for all participants, updates relations, updates voice fingerprints, appends timeline events, detects foreshadowing proposals, and checks leak risks. Example: end_scene(final_draft=<full scene text>))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "final_draft": {"type": "string", "description": "The complete final draft of the scene"}
        },
        "required": ["final_draft"]
    })";
    return s;
}

ToolMeta EndSceneTool::meta() const {
    ToolMeta m;
    m.name = "end_scene";
    m.description = "End the current scene and process its final draft";
    m.triggers = {"close scene", "conclude", "end scene", "finish scene"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> EndSceneTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string final_draft = args.value("final_draft", "");

            if (final_draft.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "无法结束场景。请提供场景的最终草稿文本。");
                return result;
            }

            auto& svc = *static_cast<EndSceneTool&>(*self).svc_;

            auto scene_opt = svc.narrative().get_scene(exec_ctx.world_id, exec_ctx.scene_id);
            if (!scene_opt) {
                result.is_error = true;
                result.output = error_response(ToolErrorCode::INTERNAL,
                    "无法读取当前场景信息。");
                return result;
            }
            if (scene_opt->status == SceneStatus::Completed) {
                result.output = error_response(ToolErrorCode::CONFLICT,
                    "场景 '" + scene_opt->title + "' 已经结束。请使用 /scene new 创建新场景。");
                return result;
            }

            auto wrapup = svc.end_scene(exec_ctx.world_id, exec_ctx.scene_id, final_draft);

            json foreshadow_proposals = json::array();
            for (auto& fp : wrapup.proposed_foreshadowing) {
                foreshadow_proposals.push_back({
                    {"id", fp.id},
                    {"content", fp.content}
                });
            }

            json warnings = json::array();
            for (auto& leak : wrapup.leak_risks) {
                warnings.push_back({
                    {"code", "CONFLICT"},
                    {"message", "泄密风险：角色 " + leak.character_id + " 可能泄露秘密 " + leak.secret_id},
                    {"detail", leak.reason}
                });
            }

            json data{
                {"pending_diary_agents", wrapup.pending_diary_agents.size()},
                {"relations_updated", wrapup.relations_updated.size()},
                {"foreshadow_proposals", foreshadow_proposals},
                {"leak_risks", wrapup.leak_risks.size()},
                {"chapter_foreshadow_stats", {
                    {"open", wrapup.chapter_foreshadow_stats.open},
                    {"paid", wrapup.chapter_foreshadow_stats.paid},
                    {"abandoned", wrapup.chapter_foreshadow_stats.abandoned}
                }}
            };

            if (warnings.empty()) {
                result.output = ok_response(data);
            } else {
                result.output = partial_success(data, warnings);
            }

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("end_scene 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== SearchAgentTool ==========

ToolSpec SearchAgentTool::spec() const {
    ToolSpec s;
    s.name = "search_agent";
    s.description = R"(Search for characters by traits and/or identity. Returns matching agents with their basic info. Example: search_agent(traits=["剑术"], identity="骑士") or search_agent(traits=["勇敢","善良"]))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "traits": {"type": "array", "items": {"type": "string"}, "description": "Traits to search for"},
            "identity": {"type": "string", "description": "Identity keyword to filter by"}
        },
        "required": []
    })";
    return s;
}

ToolMeta SearchAgentTool::meta() const {
    ToolMeta m;
    m.name = "search_agent";
    m.description = "Search for characters by traits or identity";
    m.triggers = {"agent lookup", "find character", "search"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> SearchAgentTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::vector<std::string> traits;
            if (args.contains("traits") && args["traits"].is_array()) {
                for (auto& t : args["traits"]) traits.push_back(t.get<std::string>());
            }
            std::string identity = args.value("identity", "");

            if (traits.empty() && identity.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "请至少提供 traits 或 identity 中的一个搜索条件。例如：search_agent(traits=[\"剑术\"])");
                return result;
            }

            auto& svc = *static_cast<SearchAgentTool&>(*self).svc_;

            auto agents = svc.agents().search_agents_by_traits(exec_ctx.world_id, traits, identity);

            if (agents.empty()) {
                result.output = error_response(ToolErrorCode::EMPTY_RESULT,
                    "没有找到匹配的角色。尝试更宽泛的特质或身份描述。");
                return result;
            }

            json arr = json::array();
            for (auto& ag : agents) {
                try {
                    auto card = svc.agents().load_character_card(ag.id);
                    arr.push_back({
                        {"agent_id", ag.id},
                        {"name", card.name},
                        {"identity", card.identity},
                        {"core_traits", card.core_traits},
                        {"appearance", make_snippet(card.appearance, 80)}
                    });
                } catch (...) {
                    arr.push_back({{"agent_id", ag.id}, {"name", ag.name}});
                }
            }
            result.output = ok_response({{"agents", arr}, {"count", agents.size()}});

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("search_agent 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== QueryWorldTool ==========

ToolSpec QueryWorldTool::spec() const {
    ToolSpec s;
    s.name = "query_world";
    s.description = R"(Search all world knowledge categories at once. Returns matching entries from map, history, magic, and faction categories. Example: query_world(狼烟) or query_world(北境, category=map))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "query": {"type": "string", "description": "Search query across all knowledge"},
            "category": {"type": "string", "description": "Optional category filter: map, history, magic, faction"}
        },
        "required": ["query"]
    })";
    return s;
}

ToolMeta QueryWorldTool::meta() const {
    ToolMeta m;
    m.name = "query_world";
    m.description = "Search all world knowledge categories at once";
    m.triggers = {"knowledge", "query world", "search world", "world"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> QueryWorldTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string query = args.value("query", "");

            if (query.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "请提供搜索关键词。例如：query_world(狼烟)");
                return result;
            }

            auto& svc = *static_cast<QueryWorldTool&>(*self).svc_;

            std::string category = args.value("category", "");
            auto knowledge = svc.worlds().search_world_knowledge(exec_ctx.world_id, query, category);

            if (knowledge.empty()) {
                result.output = error_response(ToolErrorCode::EMPTY_RESULT,
                    "在世界知识中没有找到与 '" + query + "' 相关的内容。");
                return result;
            }

            json arr = json::array();
            for (auto& kw : knowledge) {
                arr.push_back({
                    {"id", kw.id},
                    {"category", kw.category},
                    {"content", make_snippet(kw.content, 200)},
                    {"tags", kw.tags}
                });
            }
            result.output = ok_response({{"results", arr}, {"count", knowledge.size()}});

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("query_world 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== UpdateAgentPromptTool ==========

ToolSpec UpdateAgentPromptTool::spec() const {
    ToolSpec s;
    s.name = "update_agent_prompt";
    s.description = R"(更新角色或管理Agent的系统提示词。"
                    "输入：agent_id（要更新的Agent ID）、prompt（新的系统提示词全文）。"
                    "创建角色/管理Agent后必须调用此工具来设置其系统提示词。)";
    s.source = "worldbuilding";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "agent_id": {"type": "string", "description": "要更新提示词的Agent ID"},
            "prompt": {"type": "string", "description": "新的系统提示词全文"}
        },
        "required": ["agent_id", "prompt"]
    })";
    return s;
}

ToolMeta UpdateAgentPromptTool::meta() const {
    ToolMeta m;
    m.name = "update_agent_prompt";
    m.description = "Update an agent's system prompt";
    m.triggers = {"agent prompt", "set prompt", "system prompt", "update prompt"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> UpdateAgentPromptTool::execute(
    ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        auto& svc = *static_cast<UpdateAgentPromptTool&>(*self).svc_;
        try {
            auto args = json::parse(call.arguments);
            std::string agent_id = args.value("agent_id", "");
            std::string prompt = args.value("prompt", "");

            if (agent_id.empty() || prompt.empty()) {
                ToolResult r;
                r.call_id = call.id;
                r.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "agent_id 和 prompt 都不能为空");
                return r;
            }

            svc.update_agent_prompt(agent_id, prompt);

            ToolResult r;
            r.call_id = call.id;
            r.output = ok_response({
                {"agent_id", agent_id},
                {"message", "系统提示词已更新"}
            });
            return r;
        } catch (const std::exception& e) {
            ToolResult r;
            r.call_id = call.id;
            r.output = error_response(ToolErrorCode::INTERNAL, e.what());
            return r;
        }
    });
}

// ========== UpdateCharacterCardTool ==========

ToolSpec UpdateCharacterCardTool::spec() const {
    ToolSpec s;
    s.name = "update_character_card";
    s.description = R"(更新角色卡片的指定字段。可修改性格特征、背景故事、情感倾向、说话风格等。"
                    "示例: update_character_card(agent_id=\"agent_xxx\", fields={\"core_traits\":[\"勇敢\",\"多疑\"]}))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "agent_id": {"type": "string", "description": "要更新的角色agent_id"},
            "fields": {"type": "object", "description": "要更新的字段，支持 core_traits, background, emotional_tendency, speaking_style, core_desire, deep_fear 等"}
        },
        "required": ["agent_id", "fields"]
    })";
    return s;
}

ToolMeta UpdateCharacterCardTool::meta() const {
    ToolMeta m;
    m.name = "update_character_card";
    m.description = "Update a character's card with new traits, appearance, or background";
    m.triggers = {"edit card", "modify character", "update character"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> UpdateCharacterCardTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string agent_id = args.value("agent_id", "");

            if (agent_id.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "更新角色卡失败。缺少必填字段：agent_id。");
                return result;
            }
            if (!args.contains("fields") || !args["fields"].is_object()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "更新角色卡失败。缺少必填字段：fields，且必须为对象。");
                return result;
            }

            auto& svc = *static_cast<UpdateCharacterCardTool&>(*self).svc_;

            auto agent_opt = svc.agents().get_agent(agent_id);
            if (!agent_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + agent_id + "' 不存在。");
                return result;
            }

            if (agent_opt->world_id != exec_ctx.world_id) {
                result.output = error_response(ToolErrorCode::NO_PERMISSION,
                    "角色不属于当前世界，无法修改。");
                return result;
            }

            // version=0 means no version check
            auto updated = svc.agents().patch_character_card(agent_id, args["fields"], 0);

            result.output = ok_response({
                {"agent_id", updated.agent_id},
                {"version", updated.version}
            });

        } catch (const VersionConflictError& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::CONFLICT,
                "卡片已被其他来源修改（当前版本：" + std::to_string(e.current_version) + "），请刷新后重试");
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("update_character_card 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== WriteMyDiaryTool ==========

ToolSpec WriteMyDiaryTool::spec() const {
    ToolSpec s;
    s.name = "write_my_diary";
    s.description = R"(以第一人称记录你在刚结束的场景中的经历、感受和想法。只能写自己的日记，内容应基于你的角色视角。示例: write_my_diary(content="今天在旅店里遇到了一个可疑的陌生人...", mood="困惑"))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "content": {"type": "string", "description": "日记正文，第一人称"},
            "mood": {"type": "string", "description": "心情标签：喜悦/悲伤/愤怒/恐惧/期待/困惑/决心/平静"},
            "scene_id": {"type": "string", "description": "关联场景ID，默认为当前场景"}
        },
        "required": ["content", "mood"]
    })";
    return s;
}

ToolMeta WriteMyDiaryTool::meta() const {
    ToolMeta m;
    m.name = "write_my_diary";
    m.description = "Write a new entry to the character's personal diary";
    m.triggers = {"diary", "journal entry", "write diary"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> WriteMyDiaryTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string content = args.value("content", "");
            std::string mood = args.value("mood", "");

            if (content.empty() || mood.empty()) {
                result.is_error = true;
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "缺少必填字段：content 或 mood。");
                return result;
            }

            static const std::set<std::string> VALID_MOODS = {
                "喜悦", "悲伤", "愤怒", "恐惧", "期待", "困惑", "决心", "平静"
            };
            if (!VALID_MOODS.count(mood)) {
                result.is_error = true;
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "无效的心情标签。可选：喜悦/悲伤/愤怒/恐惧/期待/困惑/决心/平静");
                return result;
            }

            auto& svc = *static_cast<WriteMyDiaryTool&>(*self).svc_;

            std::string agent_id = exec_ctx.caller_agent_id;
            std::string scene_id = args.value("scene_id", exec_ctx.scene_id);

            auto agent_opt = svc.agents().get_agent(agent_id);
            if (!agent_opt) {
                result.is_error = true;
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色不存在。");
                return result;
            }

            std::string world_time;
            auto scene_opt = svc.narrative().get_scene(exec_ctx.world_id, scene_id);
            if (scene_opt) world_time = scene_opt->world_time;

            // Check leak risk based on the diary content (spec step 2)
            int leak_risk_level = 0;
            if (scene_opt) {
                auto risks = svc.secrets().check_leak_risk(exec_ctx.world_id, *scene_opt, content);
                if (!risks.empty()) leak_risk_level = 1;
            }

            DiaryEntry entry;
            entry.id = make_id("diary");
            entry.agent_id = agent_id;
            entry.scene_id = scene_id;
            entry.world_time = world_time;
            entry.content = content;
            entry.mood = mood;
            entry.status = "completed";
            entry.leak_risk_level = leak_risk_level;

            svc.agents().append_diary_entry(entry);

            // Emit diary_created SSE event (spec section 六)
            svc.notify_diary_created(exec_ctx.world_id, entry.id, agent_id, scene_id, mood, entry.leak_risk_level);

            nlohmann::json out = {
                {"agent_id", agent_id},
                {"scene_id", scene_id},
                {"mood", mood},
                {"leak_risk_level", entry.leak_risk_level}
            };
            result.output = out.dump();
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string(R"({"error": "write_my_diary 内部错误: )") + e.what() + "\"}";
        }
        return result;
    });
}

// ========== CompressMyMemoryTool ==========

ToolSpec CompressMyMemoryTool::spec() const {
    ToolSpec s;
    s.name = "compress_my_memory";
    s.description = R"(回顾你最近未压缩的日记条目（不超过20条），将其压缩为一份记忆摘要。
调用时机：当感觉需要整理近期记忆时，或被告知日记积累较多时。
示例: compress_my_memory())";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {},
        "required": []
    })";
    return s;
}

ToolMeta CompressMyMemoryTool::meta() const {
    ToolMeta m;
    m.name = "compress_my_memory";
    m.description = "Compress and summarize the character's memory diary entries";
    m.triggers = {"compact diary", "compress", "summarize memory"};
    m.pinned = false;
    m.intents = {IntentType::Memory};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> CompressMyMemoryTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto& svc = *static_cast<CompressMyMemoryTool&>(*self).svc_;
            int threshold = static_cast<CompressMyMemoryTool&>(*self).threshold_;
            std::string model = static_cast<CompressMyMemoryTool&>(*self).model_;
            auto& llm = *static_cast<CompressMyMemoryTool&>(*self).llm_;

            std::string agent_id = exec_ctx.caller_agent_id;
            auto diaries = svc.agents().uncompressed_diaries(agent_id, threshold);

            if (diaries.size() < 5) {
                nlohmann::json out = {
                    {"compressed", false},
                    {"message", "最近的记忆还比较清晰，暂时不需要压缩。当前未压缩日记数: " +
                        std::to_string(diaries.size())}
                };
                result.output = out.dump();
                return result;
            }

            // Build compression prompt
            std::ostringstream prompt;
            prompt << "你是一个记忆管理助手。将以下" << diaries.size()
                   << "条角色日记压缩为一份记忆摘要。\n\n";
            for (const auto& d : diaries) {
                prompt << "--- [" << d.world_time << "] 心情: " << d.mood << " ---\n";
                prompt << d.content << "\n\n";
            }
            prompt << "请以JSON格式输出摘要：\n"
                   << "{\"summary\":\"以第三人称概述这段时间内角色的经历、情感变化、关系发展和重要发现，300字以内\","
                   << "\"key_events\":[\"事件1\",\"事件2\"],"
                   << "\"emotional_arc\":\"从起始心情到结束心情的情感轨迹\","
                   << "\"relationship_changes\":\"关系变化概述，无变化则写'无明显变化'\"}";

            // Call LLM for compression
            ChatRequest req;
            req.model = model.empty() ? "gpt-4o-mini" : model;
            req.max_output_tokens = 1024;
            req.messages = {{"user", prompt.str()}};
            req.tools = {};
            req.enable_cache = false;

            auto llm_future = llm.chat(req, [](StreamChunk){}, nullptr);
            auto llm_response = llm_future.get();

            // Parse LLM response as JSON
            auto summary_json = nlohmann::json::parse(llm_response.text);

            MemorySummary summary;
            summary.agent_id = agent_id;
            summary.summary = summary_json.value("summary", llm_response.text);
            summary.period_start = diaries.front().world_time;
            summary.period_end = diaries.back().world_time;
            summary.created_at = now_iso_utc();
            for (const auto& d : diaries) summary.source_diary_ids.push_back(d.id);

            svc.agents().write_memory_summary(summary);

            // Emit memory_summary_created SSE event (spec section 六)
            svc.notify_memory_summary_created(exec_ctx.world_id, summary.id, agent_id, summary.source_diary_ids);

            nlohmann::json out = {
                {"compressed", true},
                {"summary_id", summary.id},
                {"source_count", diaries.size()},
                {"summary", summary.summary},
                {"key_events", summary_json.value("key_events", nlohmann::json::array())}
            };
            result.output = out.dump();
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string(R"({"error": "compress_my_memory 内部错误: )") + e.what() + "\"}";
        }
        return result;
    });
}

// ========== AddRelationTool ==========

ToolSpec AddRelationTool::spec() const {
    ToolSpec s;
    s.name = "add_relation";
    s.description = R"(为两个角色之间添加关系。"
                    "示例: add_relation(source_id=\"agent_xxx\", target_id=\"agent_yyy\", kind=\"同盟\", evidence=\"曾在战场上并肩作战\"))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "source_id": {"type": "string", "description": "关系发起方角色agent_id"},
            "target_id": {"type": "string", "description": "关系目标方角色agent_id"},
            "kind": {"type": "string", "description": "关系类型：同盟/敌对/亲属/师徒/隶属/拥有/使用/修炼/控制/位于/关于/其他"},
            "evidence": {"type": "string", "description": "关系成立的事实质证"}
        },
        "required": ["source_id", "target_id", "kind"]
    })";
    return s;
}

ToolMeta AddRelationTool::meta() const {
    ToolMeta m;
    m.name = "add_relation";
    m.description = "Add a relationship between two characters or entities";
    m.triggers = {"connect", "relation", "relationship"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> AddRelationTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string source_id = args.value("source_id", "");
            std::string target_id = args.value("target_id", "");
            std::string kind = args.value("kind", "");

            if (source_id.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "添加关系失败。缺少必填字段：source_id。");
                return result;
            }
            if (target_id.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "添加关系失败。缺少必填字段：target_id。");
                return result;
            }
            if (kind.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "添加关系失败。缺少必填字段：kind。可选值：同盟/敌对/亲属/师徒/隶属/拥有/使用/修炼/控制/位于/关于/其他。");
                return result;
            }

            auto& svc = *static_cast<AddRelationTool&>(*self).svc_;

            auto src_opt = svc.agents().get_agent(source_id);
            if (!src_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + source_id + "' 不存在。");
                return result;
            }
            auto tgt_opt = svc.agents().get_agent(target_id);
            if (!tgt_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + target_id + "' 不存在。");
                return result;
            }
            if (src_opt->world_id != exec_ctx.world_id) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + source_id + "' 不在当前世界中。");
                return result;
            }
            if (tgt_opt->world_id != exec_ctx.world_id) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + target_id + "' 不在当前世界中。");
                return result;
            }

            RelationEntry rel;
            rel.agent_id = source_id;
            rel.target_id = target_id;
            rel.relation_type = kind;
            rel.description = args.value("evidence", "");

            svc.agents().upsert_relation(std::move(rel));

            json data{
                {"source_id", source_id},
                {"target_id", target_id},
                {"kind", kind}
            };
            result.output = ok_response(data);

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("add_relation 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== UpdateForeshadowTool ==========

ToolSpec UpdateForeshadowTool::spec() const {
    ToolSpec s;
    s.name = "update_foreshadow";
    s.description = R"(更新伏笔的状态或回收方案。"
                    "示例: update_foreshadow(id=\"foreshadow_xxx\", status=\"paid\", pay_off_idea=\"铁匠在决战中断指的往事被揭开\"))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "id": {"type": "string", "description": "伏笔ID"},
            "status": {"type": "string", "enum": ["open", "paid", "abandoned"], "description": "新状态"},
            "pay_off_idea": {"type": "string", "description": "更新的回收方案描述"},
            "paid_at": {"type": "string", "description": "偿还时关联的场景ID"}
        },
        "required": ["id"]
    })";
    return s;
}

ToolMeta UpdateForeshadowTool::meta() const {
    ToolMeta m;
    m.name = "update_foreshadow";
    m.description = "Update a foreshadowing entry status or payoff";
    m.triggers = {"foreshadow", "payoff", "update foreshadow"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> UpdateForeshadowTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string id = args.value("id", "");

            if (id.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "更新伏笔失败。缺少必填字段：id。");
                return result;
            }

            auto& svc = *static_cast<UpdateForeshadowTool&>(*self).svc_;

            json fields;
            if (args.contains("status")) {
                std::string status = args["status"].get<std::string>();
                if (status != "open" && status != "paid" && status != "abandoned") {
                    result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                        "'" + status + "' 不是有效的伏笔状态。可选值：open / paid / abandoned。");
                    return result;
                }
                fields["status"] = status;
            }
            if (args.contains("pay_off_idea")) {
                fields["pay_off_idea"] = args["pay_off_idea"];
            }
            if (args.contains("paid_at")) {
                fields["paid_at"] = args["paid_at"];
            }

            if (fields.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "更新伏笔失败。至少需要提供一个要修改的字段（status / pay_off_idea / paid_at）。");
                return result;
            }

            bool ok = svc.foreshadowing().patch(exec_ctx.world_id, id, fields);
            if (!ok) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "伏笔 '" + id + "' 不存在或更新失败。");
                return result;
            }

            result.output = ok_response({
                {"foreshadowing_id", id},
                {"updated_fields", fields}
            });

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("update_foreshadow 内部错误: ") + e.what());
        }
        return result;
    });
}

// ========== RelationManager Tools ==========

ToolSpec QuerySubgraphTool::spec() const {
    ToolSpec s;
    s.name = "query_subgraph";
    s.description = R"(Query relations among specified entities. Returns all directed edges between them with stance, fact, and description. Example: query_subgraph(entity_names=["艾琳","卡伦","玛莎"]))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "entity_names": {"type": "array", "items": {"type": "string"}, "description": "Entity names to query relations for"}
        },
        "required": ["entity_names"]
    })";
    return s;
}

ToolMeta QuerySubgraphTool::meta() const {
    ToolMeta m;
    m.name = "query_subgraph";
    m.description = "Query the knowledge graph subgraph around specified nodes";
    m.triggers = {"graph query", "query graph", "subgraph"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> QuerySubgraphTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = json::parse(call.arguments);
            if (!args.contains("entity_names") || !args["entity_names"].is_array()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT, "entity_names (array) is required");
                return result;
            }
            std::vector<std::string> names;
            for (auto& n : args["entity_names"]) names.push_back(n.get<std::string>());

            auto& svc = *static_cast<QuerySubgraphTool&>(*self).svc_;
            auto* kg = svc.kg_provider();
            if (!kg) {
                result.output = error_response(ToolErrorCode::INTERNAL, "Knowledge Graph provider not available");
                return result;
            }
            merak::kg::QueryFilters filters;
            auto sg = kg->query_subgraph(exec_ctx.world_id, names, filters);
            auto md = merak::kg::KnowledgeGraphProvider::subgraph_to_markdown(sg);
            result.output = ok_response({{"markdown", md.empty() ? "*No relations found*" : md}, {"relation_count", sg.relations.size()}});
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL, std::string("query_subgraph failed: ") + e.what());
        }
        return result;
    });
}

ToolSpec ExpandGraphTool::spec() const {
    ToolSpec s;
    s.name = "expand_graph";
    s.description = R"(Expand from an entity to find its N-hop neighbor graph. Returns neighbors and relations. Example: expand_graph(entity_name="艾琳", radius=2))";
    s.source = "builtin";
    s.parameters_json = R"json({
        "type": "object",
        "properties": {
            "entity_name": {"type": "string", "description": "Center entity name"},
            "radius": {"type": "integer", "description": "Hop count (default: 1)", "default": 1}
        },
        "required": ["entity_name"]
    })json";
    return s;
}

ToolMeta ExpandGraphTool::meta() const {
    ToolMeta m;
    m.name = "expand_graph";
    m.description = "Expand the knowledge graph by adding inferred relations";
    m.triggers = {"expand", "graph expand", "infer relations"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> ExpandGraphTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = json::parse(call.arguments);
            std::string entity_name = args.value("entity_name", "");
            if (entity_name.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT, "entity_name is required");
                return result;
            }
            int radius = args.value("radius", 1);

            auto& svc = *static_cast<ExpandGraphTool&>(*self).svc_;
            auto* kg = svc.kg_provider();
            if (!kg) {
                result.output = error_response(ToolErrorCode::INTERNAL, "Knowledge Graph provider not available");
                return result;
            }
            merak::kg::QueryFilters filters;
            auto ng = kg->expand(exec_ctx.world_id, entity_name, radius, filters);
            auto md = merak::kg::KnowledgeGraphProvider::neighbor_graph_to_markdown(ng);
            result.output = ok_response({
                {"markdown", md.empty() ? "*No neighbors found*" : md},
                {"center", entity_name},
                {"neighbor_count", ng.neighbor_entities.size()},
                {"relation_count", ng.relations.size()}
            });
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL, std::string("expand_graph failed: ") + e.what());
        }
        return result;
    });
}

ToolSpec FindPathTool::spec() const {
    ToolSpec s;
    s.name = "find_path";
    s.description = R"(Find indirect connection paths between two entities. Returns up to 10 shortest paths ordered by hop count. Example: find_path(source="王五", target="艾琳", max_depth=4))";
    s.source = "builtin";
    s.parameters_json = R"json({
        "type": "object",
        "properties": {
            "source": {"type": "string", "description": "Source entity name"},
            "target": {"type": "string", "description": "Target entity name"},
            "max_depth": {"type": "integer", "description": "Max hop depth (default: 4)", "default": 4}
        },
        "required": ["source", "target"]
    })json";
    return s;
}

ToolMeta FindPathTool::meta() const {
    ToolMeta m;
    m.name = "find_path";
    m.description = "Find paths between nodes in the knowledge graph";
    m.triggers = {"find path", "path", "shortest path"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> FindPathTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = json::parse(call.arguments);
            std::string source = args.value("source", "");
            std::string target = args.value("target", "");
            if (source.empty() || target.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT, "source and target are required");
                return result;
            }
            int max_depth = args.value("max_depth", 4);

            auto& svc = *static_cast<FindPathTool&>(*self).svc_;
            auto* kg = svc.kg_provider();
            if (!kg) {
                result.output = error_response(ToolErrorCode::INTERNAL, "Knowledge Graph provider not available");
                return result;
            }
            merak::kg::QueryFilters filters;
            auto pr = kg->find_paths(exec_ctx.world_id, source, target, max_depth, filters);
            auto md = merak::kg::KnowledgeGraphProvider::path_result_to_markdown(pr);
            result.output = ok_response({
                {"markdown", md},
                {"found", pr.found},
                {"path_count", pr.paths.size()}
            });
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL, std::string("find_path failed: ") + e.what());
        }
        return result;
    });
}

ToolSpec CheckConsistencyTool::spec() const {
    ToolSpec s;
    s.name = "check_consistency";
    s.description = R"(Check relation consistency for an entity. Returns the entity's subgraph and neighbor data for the agent to analyze for contradictions, stale information, or unexpected changes. Example: check_consistency(entity_name="艾琳"))";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "entity_name": {"type": "string", "description": "Entity name to check consistency for"}
        },
        "required": ["entity_name"]
    })";
    return s;
}

ToolMeta CheckConsistencyTool::meta() const {
    ToolMeta m;
    m.name = "check_consistency";
    m.description = "Check consistency of worldbuilding data and relationships";
    m.triggers = {"check", "consistency", "validate"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> CheckConsistencyTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = json::parse(call.arguments);
            std::string entity_name = args.value("entity_name", "");
            if (entity_name.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT, "entity_name is required");
                return result;
            }

            auto& svc = *static_cast<CheckConsistencyTool&>(*self).svc_;
            auto* kg = svc.kg_provider();
            if (!kg) {
                result.output = error_response(ToolErrorCode::INTERNAL, "Knowledge Graph provider not available");
                return result;
            }
            merak::kg::QueryFilters filters;
            auto ng = kg->expand(exec_ctx.world_id, entity_name, 1, filters);
            auto ng_md = merak::kg::KnowledgeGraphProvider::neighbor_graph_to_markdown(ng);

            std::ostringstream report;
            report << "## Consistency Check for: " << entity_name << "\n\n";
            report << "**Direct Relations (" << ng.relations.size() << "):**\n\n";
            if (ng.relations.empty()) {
                report << "*No relations found — entity may be isolated.*\n";
            } else {
                report << ng_md;
            }
            report << "\n**Neighbors (" << ng.neighbor_entities.size() << "):** ";
            for (size_t i = 0; i < ng.neighbor_entities.size(); ++i) {
                if (i > 0) report << ", ";
                report << ng.neighbor_entities[i].name;
            }
            report << "\n";

            result.output = ok_response({
                {"markdown", report.str()},
                {"entity", entity_name},
                {"relation_count", ng.relations.size()},
                {"neighbor_count", ng.neighbor_entities.size()}
            });
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL, std::string("check_consistency failed: ") + e.what());
        }
        return result;
    });
}

// ========== ExtractSceneRelationsTool ==========

ToolSpec ExtractSceneRelationsTool::spec() const {
    ToolSpec s;
    s.name = "extract_scene_relations";
    s.description = R"(Load a scene's full text and participant list, and return a structured extraction prompt plus existing KG context. Use this to prepare for relation extraction — the returned prompt guides you through identifying character relationships from the scene narrative. After analysis, write each relation via upsert_relation. Example: extract_scene_relations(scene_id="scene-42"))";
    s.source = "builtin";
    s.parameters_json = R"json({
        "type": "object",
        "properties": {
            "scene_id": {"type": "string", "description": "Scene ID to extract relations from"},
            "include_existing": {"type": "boolean", "description": "Pre-query existing KG relations among participants (default: true)", "default": true}
        },
        "required": ["scene_id"]
    })json";
    return s;
}

ToolMeta ExtractSceneRelationsTool::meta() const {
    ToolMeta m;
    m.name = "extract_scene_relations";
    m.description = "Extract character and entity relations from scene narrative";
    m.triggers = {"extract relations", "parse relations", "scene relations"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> ExtractSceneRelationsTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = json::parse(call.arguments);
            std::string scene_id = args.value("scene_id", "");
            if (scene_id.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT, "scene_id is required");
                return result;
            }
            bool include_existing = args.value("include_existing", true);

            auto& svc = *static_cast<ExtractSceneRelationsTool&>(*self).svc_;
            auto* kg = svc.kg_provider();

            // 1. Load scene from NarrativeStore
            auto scene_opt = svc.narrative().get_scene(exec_ctx.world_id, scene_id);
            if (!scene_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "Scene not found: " + scene_id);
                return result;
            }
            auto& scene = *scene_opt;
            if (scene.narrative.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "Scene has no narrative text: " + scene_id);
                return result;
            }

            // 2. Resolve participant names from AgentStore
            std::vector<std::string> participant_names;
            participant_names.reserve(scene.participant_ids.size());
            for (const auto& pid : scene.participant_ids) {
                auto agent = svc.agents().get_agent(pid);
                if (agent) {
                    participant_names.push_back(
                        agent->display_name.empty() ? agent->name : agent->display_name);
                } else {
                    participant_names.push_back(pid); // fallback: use raw ID when agent not found
                }
            }

            std::ostringstream report;

            // 3. Pre-query existing KG relations if requested
            int existing_count = 0;
            if (include_existing && kg && !participant_names.empty()) {
                try {
                    merak::kg::QueryFilters filters;
                    auto sg = kg->query_subgraph(exec_ctx.world_id, participant_names, filters);
                    existing_count = static_cast<int>(sg.relations.size());
                    if (existing_count > 0) {
                        report << "## Existing Relations\n\n";
                        report << merak::kg::KnowledgeGraphProvider::subgraph_to_markdown(sg);
                        report << "\n";
                    }
                } catch (const std::exception&) {
                    // KG query is best-effort; continue without existing data
                }
            } else if (include_existing && !kg) {
                report << "## Existing Relations\n\n";
                report << "*Knowledge Graph provider not available — existing relations not queried.*\n\n";
            }

            // 4. Build extraction prompt via ExtractionService
            ExtractionService extraction(kg);
            auto prompt = extraction.build_extraction_prompt(
                scene.narrative, participant_names);

            report << "## Extraction Task\n\n";
            report << prompt;

            // 5. Return assembled output
            nlohmann::json data;
            data["markdown"] = report.str();
            data["scene_id"] = scene_id;
            data["scene_title"] = scene.title;
            data["participant_count"] = static_cast<int>(participant_names.size());
            data["participant_names"] = participant_names;
            data["existing_relation_count"] = existing_count;
            result.output = ok_response(data);

        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("extract_scene_relations failed: ") + e.what());
        }
        return result;
    });
}

ToolSpec UpsertRelationTool::spec() const {
    ToolSpec s;
    s.name = "upsert_relation";
    s.description = R"(Create or update a relation in the knowledge graph. Relations are bidirectional with stances. Example: upsert_relation(source_name="艾琳", target_name="卡伦", kind_en="MasterApprentice", kind_cn="师徒", a_to_b_stance="Friendly", b_to_a_stance="Admiring", fact="艾琳是卡伦的武艺师父"))";
    s.source = "builtin";
    s.parameters_json = R"json({
        "type": "object",
        "properties": {
            "source_name": {"type": "string", "description": "Source entity name"},
            "target_name": {"type": "string", "description": "Target entity name"},
            "kind_en": {"type": "string", "description": "Relation kind in English (Acquaintance, Friend, Lover, Kin, MasterApprentice, SuperiorSubordinate, Enemy, Rival, Ally, Member, Owner, Guardian, Benefactor, Grudge, Custom)"},
            "kind_cn": {"type": "string", "description": "Relation kind in Chinese (认识, 朋友, 恋人, 血缘, 师徒, 上下级, 敌对, 竞争, 合作, 隶属, 拥有, 守护, 恩人, 仇人)"},
            "a_to_b_stance": {"type": "string", "description": "A→B stance: Friendly, Admiring, Dependent, Neutral, Cautious, Distant, Suspicious, Hostile, Resentful, Fearful, Guilty, Disdainful, Unknown"},
            "b_to_a_stance": {"type": "string", "description": "B→A stance"},
            "fact": {"type": "string", "description": "One sentence fact summarizing the relation"},
            "description": {"type": "string", "description": "Detailed description of the relation"}
        },
        "required": ["source_name", "target_name", "kind_en"]
    })json";
    return s;
}

ToolMeta UpsertRelationTool::meta() const {
    ToolMeta m;
    m.name = "upsert_relation";
    m.description = "Create or update a relation in the knowledge graph";
    m.triggers = {"create relation", "update relation", "upsert"};
    m.pinned = false;
    m.intents = {IntentType::DomainWrite};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

std::future<ToolResult> UpsertRelationTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = json::parse(call.arguments);
            std::string source_name = args.value("source_name", "");
            std::string target_name = args.value("target_name", "");
            if (source_name.empty() || target_name.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT, "source_name and target_name are required");
                return result;
            }

            auto& svc = *static_cast<UpsertRelationTool&>(*self).svc_;
            auto* kg = svc.kg_provider();
            if (!kg) {
                result.output = error_response(ToolErrorCode::INTERNAL, "Knowledge Graph provider not available");
                return result;
            }

            // Resolve entity names to agent IDs to avoid phantom entity nodes
            auto agents = svc.agents().list_agents(exec_ctx.world_id);
            auto resolve_id = [&agents](const std::string& name) -> std::string {
                for (const auto& a : agents) {
                    if (a.name == name || a.display_name == name) return a.id;
                }
                return name; // fallback: use name as ID for non-agent entities
            };

            merak::kg::GraphRelation rel;
            rel.source_name = source_name;
            rel.target_name = target_name;
            rel.source_id = resolve_id(source_name);
            rel.target_id = resolve_id(target_name);
            rel.kind_en = args.value("kind_en", "Acquaintance");
            rel.kind_cn = args.value("kind_cn", "");
            rel.a_to_b_stance = merak::kg::stance_from_string(args.value("a_to_b_stance", "Neutral"));
            rel.b_to_a_stance = merak::kg::stance_from_string(args.value("b_to_a_stance", "Neutral"));
            rel.fact = args.value("fact", "");
            rel.description = args.value("description", "");
            rel.world_id = exec_ctx.world_id;

            kg->upsert_relation(rel);
            result.output = ok_response({
                {"source_name", source_name},
                {"target_name", target_name},
                {"kind_en", rel.kind_en},
                {"status", "upserted"}
            });
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL, std::string("upsert_relation failed: ") + e.what());
        }
        return result;
    });
}

// ========== DelegateToWriterTool ==========

ToolSpec DelegateToWriterTool::spec() const {
    ToolSpec s;
    s.name = "delegate_to_writer";
    s.description = R"(Send a structured material package to the Writer Agent to produce polished scene prose. The material package must include: scene outline, character dialogue log, relevant domain data, and writing constraints (style, POV, word count, foreshadowing). Returns the Writer's scene text.)";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "material_package": {"type": "string", "description": "Complete scene material package in markdown format"}
        },
        "required": ["material_package"]
    })";
    return s;
}

ToolMeta DelegateToWriterTool::meta() const {
    ToolMeta m;
    m.name = "delegate_to_writer";
    m.description = "Delegate scene prose writing to Writer Agent";
    m.triggers = {"writer", "prose", "compile", "scene text"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 30;
    return m;
}

std::future<ToolResult> DelegateToWriterTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string material_package = args.value("material_package", "");

            if (material_package.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "delegate_to_writer requires material_package parameter");
                return result;
            }

            auto& tool = static_cast<DelegateToWriterTool&>(*self);

            // Load Writer Agent prompt
            auto writer_prompt = prompts::load_writer_prompt();
            if (writer_prompt.empty()) {
                result.output = error_response(ToolErrorCode::INTERNAL,
                    "Writer Agent prompt (writer.md) not found");
                return result;
            }

            // Create empty tool registry (Writer has zero tools)
            auto sub_tools = std::make_shared<ToolRegistry>();

            // Create memory store (fresh, no history)
            auto memory = std::make_shared<MemoryStore>(MemoryConfig{}, nullptr);

            // Create token counter and compactor
            auto counter = std::make_shared<TokenCounter>();
            auto comp = std::make_shared<Compactor>(tool.llm_, counter);

            // Configure Writer Agent loop
            AgentLoop::Config cfg;
            cfg.system_prompt = writer_prompt;
            cfg.max_turns = 1;           // Single-turn: Writer has no tools
            cfg.default_model = tool.default_model_;
            cfg.max_output_tokens = 8192; // Allow room for full scene prose
            cfg.model_max_tokens = 128000;
            cfg.enable_compaction = false;
            cfg.enable_cache = true;

            // Create sub-loop (no worldbuilding service needed — Writer has no tools)
            AgentLoop sub_loop(cfg, tool.llm_, sub_tools, memory, comp,
                               nullptr,  // no WorldbuildingService needed
                               nullptr); // no skills registry needed

            if (!exec_ctx.world_id.empty()) sub_loop.set_active_world_id(exec_ctx.world_id);
            if (!exec_ctx.scene_id.empty()) sub_loop.set_active_scene_id(exec_ctx.scene_id);
            if (!exec_ctx.caller_agent_id.empty()) sub_loop.set_caller_agent_id(exec_ctx.caller_agent_id);

            NullRunControl control;
            auto response = sub_loop.run(material_package, control).get();

            result.output = ok_response({{"scene_text", response.text}});
            result.is_error = false;

        } catch (const std::exception& e) {
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("Writer Agent failed: ") + e.what());
            result.is_error = true;
        }

        return result;
    });
}

// ========== WorldbuildingTools Factory ==========

std::vector<ToolSpec> WorldbuildingTools::specs_for(AgentKind kind) const {
    auto tools = create_tools(kind);
    std::vector<ToolSpec> specs;
    for (auto& t : tools) specs.push_back(t->spec());
    return specs;
}

std::vector<std::unique_ptr<Tool>>
WorldbuildingTools::create_tools(AgentKind kind) const {
    std::vector<std::unique_ptr<Tool>> tools;

    switch (kind) {
    case AgentKind::God:
        tools.push_back(std::make_unique<ReadCharacterCardTool>(*service_));
        tools.push_back(std::make_unique<ReadSecretTool>(*service_));
        tools.push_back(std::make_unique<ReadForeshadowingTool>(*service_));
        tools.push_back(std::make_unique<ListOpenForeshadowingTool>(*service_));
        tools.push_back(std::make_unique<SearchAgentTool>(*service_));
        tools.push_back(std::make_unique<QueryWorldTool>(*service_));
        tools.push_back(std::make_unique<AdvanceWorldTimeTool>(*service_));
        tools.push_back(std::make_unique<CreateCharacterTool>(*service_));
        tools.push_back(std::make_unique<CreateSceneTool>(*service_));
        tools.push_back(std::make_unique<CreateChapterTool>(*service_));
        tools.push_back(std::make_unique<PlantForeshadowingTool>(*service_));
        tools.push_back(std::make_unique<ExposeSecretTool>(*service_));
        tools.push_back(std::make_unique<EndSceneTool>(*service_));
        tools.push_back(std::make_unique<CreateArcTool>(*service_));
        tools.push_back(std::make_unique<CreateSecretTool>(*service_));
        tools.push_back(std::make_unique<AddWorldKnowledgeTool>(*service_));
        tools.push_back(std::make_unique<CreateLocationTool>(*service_));
        tools.push_back(
            std::make_unique<UpdateAgentPromptTool>(*service_));
        tools.push_back(
            std::make_unique<UpdateCharacterCardTool>(*service_));
        tools.push_back(
            std::make_unique<AddRelationTool>(*service_));
        tools.push_back(
            std::make_unique<UpdateForeshadowTool>(*service_));
        tools.push_back(
            std::make_unique<DelegateToWriterTool>(*service_, llm_, writer_model_));
        break;
    case AgentKind::Individual:
        tools.push_back(std::make_unique<DescribeCharacterTool>(*service_));
        tools.push_back(std::make_unique<SearchMyDiaryTool>(*service_));
        tools.push_back(std::make_unique<LookAroundTool>(*service_));
        tools.push_back(std::make_unique<WriteMyDiaryTool>(*service_));
        tools.push_back(std::make_unique<CompressMyMemoryTool>(*service_, llm_, compression_threshold_, diary_model_));
        break;
    case AgentKind::MapManager:
        tools.push_back(std::make_unique<QueryMapTool>(*service_));
        break;
    case AgentKind::HistoryManager:
        tools.push_back(std::make_unique<QueryHistoryTool>(*service_));
        break;
    case AgentKind::MagicSystemManager:
        tools.push_back(std::make_unique<QueryMagicTool>(*service_));
        break;
    case AgentKind::FactionManager:
        tools.push_back(std::make_unique<QueryFactionTool>(*service_));
        break;
    case AgentKind::RelationManager:
        tools.push_back(std::make_unique<QuerySubgraphTool>(*service_));
        tools.push_back(std::make_unique<ExpandGraphTool>(*service_));
        tools.push_back(std::make_unique<FindPathTool>(*service_));
        tools.push_back(std::make_unique<CheckConsistencyTool>(*service_));
        tools.push_back(std::make_unique<ExtractSceneRelationsTool>(*service_));
        tools.push_back(std::make_unique<UpsertRelationTool>(*service_));
        break;
    case AgentKind::Group:
        break;
    case AgentKind::Writer:
        break;
    }

    return tools;
}

} // namespace merak::worldbuilding
