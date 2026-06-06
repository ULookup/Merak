#include <merak/worldbuilding/worldbuilding_tools.hpp>
#include <nlohmann/json.hpp>
#include <future>
#include <algorithm>

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

std::future<ToolResult> DescribeCharacterTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<DescribeCharacterTool&>(*self).ctx_;

            auto agent_opt = svc.agents().get_agent(target_id);
            if (!agent_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + target_id + "' 不存在于世界中。");
                return result;
            }

            auto scene_opt = svc.narrative().get_scene(ctx.world_id, ctx.scene_id);
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

std::future<ToolResult> SearchMyDiaryTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<SearchMyDiaryTool&>(*self).ctx_;

            auto entries = svc.agents().search_diary(ctx.caller_agent_id, query, 5);
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

std::future<ToolResult> LookAroundTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto& svc = *static_cast<LookAroundTool&>(*self).svc_;
            auto& ctx = static_cast<LookAroundTool&>(*self).ctx_;

            auto scene_opt = svc.narrative().get_scene(ctx.world_id, ctx.scene_id);
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

std::future<ToolResult> QueryMapTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<QueryMapTool&>(*self).ctx_;

            auto results = svc.worlds().search_world_knowledge(ctx.world_id, region, "map", 1);
            if (!results.empty()) {
                result.output = ok_response({{"region", region}, {"data", results[0].content}});
                return result;
            }

            auto knowledge = svc.worlds().get_world_knowledge(ctx.world_id, "map");
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

std::future<ToolResult> QueryHistoryTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<QueryHistoryTool&>(*self).ctx_;

            auto knowledge = svc.worlds().search_world_knowledge(ctx.world_id, keyword, "history");
            json arr = json::array();
            for (auto& kw : knowledge) {
                arr.push_back({
                    {"world_time", kw.created_at},
                    {"description", kw.content}
                });
            }

            if (arr.empty()) {
                std::string hint;
                auto all_history = svc.worlds().get_world_knowledge(ctx.world_id, "history");
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

std::future<ToolResult> QueryMagicTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<QueryMagicTool&>(*self).ctx_;

            auto results = svc.worlds().search_world_knowledge(ctx.world_id, topic, "magic", 1);
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

std::future<ToolResult> QueryFactionTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<QueryFactionTool&>(*self).ctx_;

            auto results = svc.worlds().search_world_knowledge(ctx.world_id, name, "faction", 1);
            if (!results.empty()) {
                result.output = ok_response({{"name", name}, {"info", results[0].content}});
                return result;
            }

            auto knowledge = svc.worlds().get_world_knowledge(ctx.world_id, "faction");
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

std::future<ToolResult> ReadCharacterCardTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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

std::future<ToolResult> ReadSecretTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string secret_id = args.value("secret_id", "");

            auto& svc = *static_cast<ReadSecretTool&>(*self).svc_;
            auto& ctx = static_cast<ReadSecretTool&>(*self).ctx_;

            auto secret_opt = find_secret_by_id(svc.secrets(), ctx.world_id, secret_id);
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

std::future<ToolResult> ReadForeshadowingTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string foreshadowing_id = args.value("foreshadowing_id", "");

            auto& svc = *static_cast<ReadForeshadowingTool&>(*self).svc_;
            auto& ctx = static_cast<ReadForeshadowingTool&>(*self).ctx_;

            auto f_opt = find_foreshadowing_by_id(svc.foreshadowing(), ctx.world_id, foreshadowing_id);
            if (!f_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "伏笔 '" + foreshadowing_id + "' 不存在。");
                return result;
            }

            if (f_opt->status == ForeshadowStatus::Paid) {
                result.output = error_response(ToolErrorCode::CONFLICT,
                    "伏笔 '" + foreshadowing_id + "' 已偿还（paid），不可再次偿还。");
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

std::future<ToolResult> ListOpenForeshadowingTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto& svc = *static_cast<ListOpenForeshadowingTool&>(*self).svc_;
            auto& ctx = static_cast<ListOpenForeshadowingTool&>(*self).ctx_;

            auto items = svc.foreshadowing().list(ctx.world_id, ForeshadowStatus::Open);
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

std::future<ToolResult> AdvanceWorldTimeTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<AdvanceWorldTimeTool&>(*self).ctx_;

            // Backward time travel check against current scene time
            {
                auto scene_opt = svc.narrative().get_scene(ctx.world_id, ctx.scene_id);
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
            auto recorded = svc.narrative().record_timeline_event(ctx.world_id, std::move(ev));

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

std::future<ToolResult> CreateCharacterTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<CreateCharacterTool&>(*self).ctx_;

            auto existing = svc.agents().list_agents(ctx.world_id);
            for (auto& ag : existing) {
                if (ag.name == name) {
                    result.output = error_response(ToolErrorCode::CONFLICT,
                        "世界 '" + ctx.world_id + "' 中已存在名为 '" + name + "' 的角色。");
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

            auto record = svc.agents().create_character(ctx.world_id, std::move(card));
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

std::future<ToolResult> CreateSceneTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<CreateSceneTool&>(*self).ctx_;

            auto preview = svc.build_scene_preview(ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(ctx.world_id, "create_scene", args, preview);

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

std::future<ToolResult> CreateChapterTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<CreateChapterTool&>(*self).ctx_;

            auto preview = svc.build_chapter_preview(ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(ctx.world_id, "create_chapter", args, preview);

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

std::future<ToolResult> CreateArcTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<CreateArcTool&>(*self).ctx_;

            auto preview = svc.build_arc_preview(ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(ctx.world_id, "create_arc", args, preview);

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

std::future<ToolResult> CreateSecretTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<CreateSecretTool&>(*self).ctx_;

            auto preview = svc.build_secret_preview(ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(ctx.world_id, "create_secret", args, preview);

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

std::future<ToolResult> AddWorldKnowledgeTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<AddWorldKnowledgeTool&>(*self).ctx_;

            auto preview = svc.build_world_knowledge_preview(ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(ctx.world_id, "add_world_knowledge", args, preview);

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

std::future<ToolResult> CreateLocationTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<CreateLocationTool&>(*self).ctx_;

            auto preview = svc.build_location_preview(ctx.world_id, args);
            auto creation_id = svc.store_pending_creation(ctx.world_id, "create_location", args, preview);

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

std::future<ToolResult> PlantForeshadowingTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<PlantForeshadowingTool&>(*self).ctx_;

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

            auto planted = svc.foreshadowing().plant(ctx.world_id, std::move(f));
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

std::future<ToolResult> ExposeSecretTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string secret_id = args.value("secret_id", "");

            auto& svc = *static_cast<ExposeSecretTool&>(*self).svc_;
            auto& ctx = static_cast<ExposeSecretTool&>(*self).ctx_;

            auto secret_opt = find_secret_by_id(svc.secrets(), ctx.world_id, secret_id);
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

            auto exposed = svc.secrets().expose(ctx.world_id, secret_id, ctx.scene_id);

            json warnings = json::array();
            int repaid = 0;
            for (auto& f_id : exposed.related_foreshadowing_ids) {
                try {
                    svc.foreshadowing().pay(ctx.world_id, f_id, ctx.scene_id);
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

std::future<ToolResult> EndSceneTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<EndSceneTool&>(*self).ctx_;

            auto scene_opt = svc.narrative().get_scene(ctx.world_id, ctx.scene_id);
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

            auto wrapup = svc.end_scene(ctx.world_id, ctx.scene_id, final_draft);

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
                {"diaries_written", wrapup.diaries_written.size()},
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

std::future<ToolResult> SearchAgentTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<SearchAgentTool&>(*self).ctx_;

            auto agents = svc.agents().search_agents_by_traits(ctx.world_id, traits, identity);

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

std::future<ToolResult> QueryWorldTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
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
            auto& ctx = static_cast<QueryWorldTool&>(*self).ctx_;

            std::string category = args.value("category", "");
            auto knowledge = svc.worlds().search_world_knowledge(ctx.world_id, query, category);

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

std::future<ToolResult> UpdateAgentPromptTool::execute(
    ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
        auto& svc = *static_cast<UpdateAgentPromptTool&>(*self).svc_;
        try {
            auto args = json::parse(call.arguments);
            std::string agent_id = args.value("agent_id", "");
            std::string prompt = args.value("prompt", "");

            if (agent_id.empty() || prompt.empty()) {
                ToolResult r;
                r.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "agent_id 和 prompt 都不能为空");
                return r;
            }

            svc.update_agent_prompt(agent_id, prompt);

            ToolResult r;
            r.output = ok_response({
                {"agent_id", agent_id},
                {"message", "系统提示词已更新"}
            });
            return r;
        } catch (const std::exception& e) {
            ToolResult r;
            r.output = error_response(ToolErrorCode::INTERNAL, e.what());
            return r;
        }
    });
}

// ========== WorldbuildingTools Factory ==========

std::vector<ToolSpec> WorldbuildingTools::specs_for(AgentKind kind) const {
    ToolContext empty_ctx;
    auto tools = create_tools(kind, empty_ctx);
    std::vector<ToolSpec> specs;
    for (auto& t : tools) specs.push_back(t->spec());
    return specs;
}

std::vector<std::unique_ptr<Tool>>
WorldbuildingTools::create_tools(AgentKind kind, const ToolContext& ctx) const {
    std::vector<std::unique_ptr<Tool>> tools;

    switch (kind) {
    case AgentKind::God:
        tools.push_back(std::make_unique<ReadCharacterCardTool>(*service_, ctx));
        tools.push_back(std::make_unique<ReadSecretTool>(*service_, ctx));
        tools.push_back(std::make_unique<ReadForeshadowingTool>(*service_, ctx));
        tools.push_back(std::make_unique<ListOpenForeshadowingTool>(*service_, ctx));
        tools.push_back(std::make_unique<SearchAgentTool>(*service_, ctx));
        tools.push_back(std::make_unique<QueryWorldTool>(*service_, ctx));
        tools.push_back(std::make_unique<AdvanceWorldTimeTool>(*service_, ctx));
        tools.push_back(std::make_unique<CreateCharacterTool>(*service_, ctx));
        tools.push_back(std::make_unique<CreateSceneTool>(*service_, ctx));
        tools.push_back(std::make_unique<CreateChapterTool>(*service_, ctx));
        tools.push_back(std::make_unique<PlantForeshadowingTool>(*service_, ctx));
        tools.push_back(std::make_unique<ExposeSecretTool>(*service_, ctx));
        tools.push_back(std::make_unique<EndSceneTool>(*service_, ctx));
        tools.push_back(
            std::make_unique<UpdateAgentPromptTool>(*service_, ctx));
        break;
    case AgentKind::Individual:
        tools.push_back(std::make_unique<DescribeCharacterTool>(*service_, ctx));
        tools.push_back(std::make_unique<SearchMyDiaryTool>(*service_, ctx));
        tools.push_back(std::make_unique<LookAroundTool>(*service_, ctx));
        break;
    case AgentKind::MapManager:
        tools.push_back(std::make_unique<QueryMapTool>(*service_, ctx));
        break;
    case AgentKind::HistoryManager:
        tools.push_back(std::make_unique<QueryHistoryTool>(*service_, ctx));
        break;
    case AgentKind::MagicSystemManager:
        tools.push_back(std::make_unique<QueryMagicTool>(*service_, ctx));
        break;
    case AgentKind::FactionManager:
        tools.push_back(std::make_unique<QueryFactionTool>(*service_, ctx));
        break;
    case AgentKind::Group:
        break;
    }

    return tools;
}

} // namespace merak::worldbuilding
