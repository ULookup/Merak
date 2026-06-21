#include <merak/worldbuilding/scene_orchestrator.hpp>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/worldbuilding_tools.hpp>
#include <merak/kg/kg_provider.hpp>

#include "prompts/character.hpp"
#include "prompts/creative_director.hpp"
#include "prompts/domain_manager.hpp"
#include "prompts/relation_manager.hpp"
#include "prompts/group.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace merak::worldbuilding {
namespace {

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to read " + path.string());
    }
    return nlohmann::json::parse(input);
}

void ensure_world_exists(WorldStore& worlds, const std::string& world_id) {
    if (!worlds.get_world(world_id).has_value()) {
        throw std::runtime_error("unknown world: " + world_id);
    }
}

std::string load_scene_narrative(WorldStore& worlds,
                                  const std::string& world_id,
                                  const std::string& scene_id) {
    const auto path = worlds.world_path(world_id) / "scenes" / (scene_id + ".json");
    if (!std::filesystem::exists(path)) return {};
    return read_json(path).value("narrative", "");
}

Scene load_scene(WorldStore& worlds,
                 const std::string& world_id,
                 const std::string& scene_id) {
    const auto path = worlds.world_path(world_id) / "scenes" / (scene_id + ".json");
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("unknown scene: " + scene_id);
    }
    auto json = read_json(path);
    Scene scene;
    scene.id = json.at("id").get<std::string>();
    scene.title = json.at("title").get<std::string>();
    scene.chapter_id = json.at("chapter_id").get<std::string>();
    scene.world_time = json.at("world_time").get<std::string>();
    scene.narrative = json.value("narrative", "");
    if (!json.at("section_id").is_null()) {
        scene.section_id = json.at("section_id").get<std::string>();
    }
    if (!json.at("location_id").is_null()) {
        scene.location_id = json.at("location_id").get<std::string>();
    }
    scene.participant_ids =
        json.at("participant_ids").get<std::vector<std::string>>();
    auto status_str = json.value("status", "draft");
    if (status_str == "draft") scene.status = SceneStatus::Draft;
    else if (status_str == "writing") scene.status = SceneStatus::Writing;
    else if (status_str == "completed") scene.status = SceneStatus::Completed;
    return scene;
}

std::vector<std::string> split_dialogue(const std::string& markdown) {
    std::vector<std::string> lines;
    std::istringstream in(markdown);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

Foreshadowing detect_foreshadow_proposal(const std::string& markdown) {
    Foreshadowing proposal;
    proposal.created_by = ForeshadowCreatedBy::GodAgentDetected;

    // Narrative foreshadowing pattern detection using n-gram matching.
    // Patterns are grouped by signal strength.
    static const std::vector<std::string> strong_signals = {
        "预感", "不安的预感", "不详", "命运的",
    };
    static const std::vector<std::string> medium_signals = {
        "忽然", "似乎", "隐约", "莫名", "不禁想起",
        "意味深长", "似曾相识", "那把", "那只",
        "日后", "总有一天",
    };
    static const std::vector<std::string> weak_signals = {
        "……", "——", "或许", "也许", "仿佛",
        "不知为何", "说不上",
    };

    std::string best_line;
    int best_score = 0;
    size_t pos = 0;
    while (pos < markdown.size()) {
        size_t end = markdown.find_first_of("。\n！？!?", pos);
        if (end == std::string::npos) end = markdown.size();
        if (end > pos) {
            std::string sentence = markdown.substr(pos, end - pos);
            int score = 0;
            for (auto& p : strong_signals)
                if (sentence.find(p) != std::string::npos) score += 3;
            for (auto& p : medium_signals)
                if (sentence.find(p) != std::string::npos) score += 2;
            for (auto& p : weak_signals)
                if (sentence.find(p) != std::string::npos) score += 1;
            // Questions suggest unresolved threads
            if (sentence.find("？") != std::string::npos || sentence.find('?') != std::string::npos) score += 1;
            // Interrupted dialogue
            if (sentence.find("——") != std::string::npos && sentence.find("？") == std::string::npos) score += 1;

            if (score > best_score) {
                best_score = score;
                best_line = sentence;
            }
        }
        pos = end + 1;
    }

    if (best_score >= 3) {
        proposal.content = best_line.size() > 100 ? best_line.substr(0, 97) + "..." : best_line;
        proposal.pay_off_idea = "(检测到" + std::to_string(best_score) + "个伏笔信号，由作者确认)";
        proposal.hint_level = ForeshadowHintLevel::Visible;
    } else if (best_score >= 2) {
        proposal.content = best_line.size() > 100 ? best_line.substr(0, 97) + "..." : best_line;
        proposal.pay_off_idea = "(弱信号，由作者确认)";
        proposal.hint_level = ForeshadowHintLevel::Subtle;
    } else {
        proposal.content = "";
        proposal.pay_off_idea = "";
    }
    return proposal;
}

std::string card_to_prompt(const CharacterCard& card) {
    std::ostringstream out;
    out << "名称: " << card.name << "\n";
    out << "性别: " << card.gender << "\n";
    out << "年龄: " << card.age << "\n";
    out << "种族: " << card.race << "\n";
    out << "身份: " << card.identity << "\n";
    out << "情感倾向: " << card.emotional_tendency << "\n";
    out << "说话风格: " << card.speaking_style << "\n";
    out << "核心欲望: " << card.core_desire << "\n";
    out << "深层恐惧: " << card.deep_fear << "\n";
    out << "外表: " << card.appearance << "\n";
    out << "背景: " << card.background << "\n";
    if (!card.core_traits.empty()) {
        out << "性格特质: ";
        for (size_t i = 0; i < card.core_traits.size(); ++i) {
            if (i > 0) out << ", ";
            out << card.core_traits[i];
        }
        out << "\n";
    }
    out << "知识范围: " << card.knowledge_scope << "\n";
    return out.str();
}

} // namespace

SceneOrchestrator::SceneOrchestrator(WorldStore& worlds,
                                     AgentStore& agents,
                                     NarrativeStore& narrative,
                                     ForeshadowingStore& foreshadowing,
                                     SecretStore& secrets,
                                     VoiceAnalyzer& voice,
                                     merak::kg::KnowledgeGraphProvider* kg_provider)
    : worlds_(worlds),
      agents_(agents),
      narrative_(narrative),
      foreshadowing_(foreshadowing),
      secrets_(secrets),
      voice_(voice),
      kg_provider_(kg_provider) {}

ScenePreparation
SceneOrchestrator::prepare_scene(const std::string& world_id,
                                  const std::string& scene_id,
                                  WorldbuildingService& service) const {
    ensure_world_exists(worlds_, world_id);

    auto scene = load_scene(worlds_, world_id, scene_id);
    auto chapter_ctx = narrative_.chapter_context(world_id, scene.chapter_id);

    ScenePreparation prep;

    // God context: full knowledge
    {
        std::ostringstream god;
        god << "## 本章节概要\n";
        god << chapter_ctx.chapter_pitch << "\n\n";

        if (chapter_ctx.arc_purpose.has_value()) {
            god << "## 弧线目标\n";
            god << *chapter_ctx.arc_purpose << "\n\n";
        }

        god << "## 已有场景摘要\n";
        for (const auto& summary : chapter_ctx.previous_scene_summaries) {
            god << "- " << summary << "\n";
        }

        god << "\n## 当前场景\n";
        god << "标题: " << scene.title << "\n";
        god << "世界时间: " << scene.world_time << "\n";
        god << "参与者: ";
        for (size_t i = 0; i < scene.participant_ids.size(); ++i) {
            if (i > 0) god << ", ";
            god << scene.participant_ids[i];
        }
        god << "\n\n";

        // World knowledge
        auto knowledge = worlds_.get_world_knowledge(world_id, "");
        if (!knowledge.empty()) {
            god << "## 世界观知识\n";
            for (const auto& wk : knowledge) {
                god << "- [" << wk.category << "] " << wk.content << "\n";
            }
            god << "\n";
        }

        // Relevant foreshadowing
        auto relevant = foreshadowing_.relevant_for_scene(world_id, scene);
        prep.relevant_foreshadowing = relevant;
        if (!relevant.empty()) {
            god << "## 相关伏笔\n";
            for (const auto& fs : relevant) {
                god << "- " << fs.content << " (提示: " << to_string(fs.hint_level) << ")\n";
            }
            god << "\n";
        }

        // Open foreshadowing
        if (!chapter_ctx.open_foreshadowing_ids.empty()) {
            god << "## 未偿还伏笔\n";
            for (const auto& id : chapter_ctx.open_foreshadowing_ids) {
                god << "- " << id << "\n";
            }
            god << "\n";
        }

        // Knowledge Graph: relation subgraph for scene participants
        if (kg_provider_ && !scene.participant_ids.empty()) {
            std::vector<std::string> participant_names;
            for (const auto& pid : scene.participant_ids) {
                try {
                    auto agent = agents_.get_agent(pid);
                    if (agent) participant_names.push_back(agent->name);
                } catch (const std::exception& e) {
                    spdlog::warn("get_agent for KG participant names skipped: {}", e.what());
                }
            }
            if (participant_names.size() > 1) {
                try {
                    merak::kg::QueryFilters filters;
                    auto sg = kg_provider_->query_subgraph(world_id, participant_names, filters);
                    auto md = merak::kg::KnowledgeGraphProvider::subgraph_to_markdown(sg);
                    if (!md.empty()) {
                        god << md << "\n";
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("KG query_subgraph skipped: {}", e.what());
                }
            }
        }

        // Append creative director prompt
        auto cd_prompt = prompts::load_creative_director(prompts_dir_);
        if (!cd_prompt.empty()) {
            god << "\n" << cd_prompt << "\n";
        }

        prep.god_context = god.str();
    }

    // Character views with knowledge barriers
    auto secret_views = secrets_.scene_asymmetry(world_id, scene);
    prep.secret_views = secret_views;

    for (const auto& pid : scene.participant_ids) {
        CharacterContextView view;
        view.agent_id = pid;

        std::ostringstream prompt;
        try {
            auto card = agents_.load_character_card(pid);
            prompt << card_to_prompt(card);
        } catch (const std::exception& e) {
            spdlog::warn("load_character_card({}) skipped: {}", pid, e.what());
            // Agent may be a manager or group; skip card for non-characters
            prompt << "代理人: " << pid << "\n";
        }

        // Secret-filtered context
        auto it = std::find_if(secret_views.begin(), secret_views.end(),
                                [&](const KnowledgeView& kv) { return kv.character_id == pid; });
        if (it != secret_views.end() && !it->context_snippet.empty()) {
            prompt << "\n## 你知道的信息\n";
            prompt << it->context_snippet << "\n";
        }

        // Recent diary memory (index + summaries)
        try {
            auto diary_headers = agents_.recent_diary_headers(pid, service.diary_context_limit());

            // Memory summaries first (compressed long-term memory)
            auto summaries = agents_.recent_summaries(pid, 10);
            if (!summaries.empty()) {
                prompt << "\n## 你的记忆摘要\n";
                for (const auto& s : summaries) {
                    prompt << "- " << s.period_start << " ~ " << s.period_end << ": "
                           << s.summary.substr(0, 150) << "\n";
                }
            }

            // Diary index (preview headers)
            if (!diary_headers.empty()) {
                prompt << "\n## 你的日记索引 (使用 read_diary_entry 查看全文)\n";
                for (const auto& d : diary_headers) {
                    prompt << "- [" << d.world_time << "] " << d.mood << " | "
                           << d.content << "  (id=" << d.id << ")\n";
                    view.loaded_memory_refs.push_back(d.id);
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("diary index loading skipped: {}", e.what());
        }

        // Group shared memory: if agent is a group member, load shared refs
        try {
            auto shared = agents_.shared_memory_refs_for(pid);
            for (const auto& ref : shared) {
                view.loaded_memory_refs.push_back(ref);
            }
        } catch (const std::exception& e) {
            spdlog::warn("shared_memory_refs_for skipped: {}", e.what());
        }

        // Append character behavior prompt
        auto char_prompt = prompts::load_character_prompt(prompts_dir_);
        if (!char_prompt.empty()) {
            prompt << "\n" << char_prompt << "\n";
        }

        view.system_prompt = prompt.str();
        prep.character_views.push_back(view);
    }

    // Populate tools_by_agent_id for each agent.
    WorldbuildingTools tools_factory(service);

    // God agent.
    {
        auto instances = tools_factory.create_tools(AgentKind::God);
        for (auto& t : instances) prep.tools_by_agent_id["god"].push_back(t->spec());
    }

    // Character tools per participant.
    for (auto& cv : prep.character_views) {
        auto instances = tools_factory.create_tools(AgentKind::Individual);
        for (auto& t : instances) prep.tools_by_agent_id[cv.agent_id].push_back(t->spec());
    }

    // Manager tools and behavior constraints.
    auto dm_prompt = prompts::load_domain_manager_prompt(prompts_dir_);
    for (auto kind : {AgentKind::MapManager, AgentKind::HistoryManager,
                       AgentKind::MagicSystemManager, AgentKind::FactionManager}) {
        auto instances = tools_factory.create_tools(kind);
        std::string key = to_string(kind);
        for (auto& t : instances) prep.tools_by_agent_id[key].push_back(t->spec());
        if (!dm_prompt.empty()) {
            prep.behavior_constraints[key] = dm_prompt;
        }
    }

    // RelationManager gets its own prompt with KG tool semantics
    {
        auto kind = AgentKind::RelationManager;
        auto instances = tools_factory.create_tools(kind);
        std::string key = to_string(kind);
        for (auto& t : instances) prep.tools_by_agent_id[key].push_back(t->spec());
        auto rm_prompt = prompts::load_relation_manager_prompt(prompts_dir_);
        if (!rm_prompt.empty()) {
            prep.behavior_constraints[key] = rm_prompt;
        }
    }

    // Group managers — cultural context layer, one per group
    {
        auto gp_prompt_template = prompts::load_group_prompt(prompts_dir_);

        try {
            auto all_agents = agents_.list_agents(world_id);
            for (const auto& ag : all_agents) {
                if (ag.kind != AgentKind::Group) continue;

                auto instances = tools_factory.create_tools(AgentKind::Group);
                for (auto& t : instances) {
                    prep.tools_by_agent_id[ag.id].push_back(t->spec());
                }

                if (!gp_prompt_template.empty()) {
                    std::string prompt = gp_prompt_template;
                    size_t pos = 0;
                    while ((pos = prompt.find("{{agent.name}}", pos)) != std::string::npos) {
                        prompt.replace(pos, 14, ag.name);
                        pos += ag.name.length();
                    }
                    prep.behavior_constraints[ag.id] = prompt;
                }
            }
        } catch (...) {
            // Fallback: single shared key for backward compatibility
            auto instances = tools_factory.create_tools(AgentKind::Group);
            std::string key = to_string(AgentKind::Group);
            for (auto& t : instances) prep.tools_by_agent_id[key].push_back(t->spec());
            if (!gp_prompt_template.empty()) {
                prep.behavior_constraints[key] = gp_prompt_template;
            }
        }
    }

    return prep;
}

SceneWrapUp SceneOrchestrator::finish_scene(const std::string& world_id,
                                             const std::string& scene_id,
                                             const std::string& final_markdown) {
    ensure_world_exists(worlds_, world_id);

    auto scene = load_scene(worlds_, world_id, scene_id);
    SceneWrapUp wrap;

    // Append narrative to scene
    narrative_.append_scene_text(world_id, scene_id, final_markdown);
    narrative_.update_scene_status(world_id, scene_id, SceneStatus::Completed);

    // Mark participants that should write diaries
    for (const auto& pid : scene.participant_ids) {
        wrap.pending_diary_agents.push_back(pid);
    }

    // Voice fingerprint update from dialogue lines
    auto dialogue_lines = split_dialogue(final_markdown);
    if (dialogue_lines.size() >= 10) {
        for (const auto& pid : scene.participant_ids) {
            try {
                voice_.update(pid, dialogue_lines);
            } catch (const std::exception& e) {
                spdlog::warn("voice fingerprint update skipped for {}: {}", pid, e.what());
                wrap.warnings.push_back("角色 " + pid + " 语音指纹更新失败");
            }
        }
    }

    // Timeline event
    {
        TimelineEvent event;
        event.world_time = scene.world_time;
        event.description = "场景完成: " + scene.title;
        event.recorded_by = "god";
        event.affected_character_ids = scene.participant_ids;
        event.related_scene_ids = {scene_id};
        narrative_.record_timeline_event(world_id, event);
    }

    // Foreshadowing detection proposals
    {
        auto proposal = detect_foreshadow_proposal(final_markdown);
        if (!proposal.content.empty()) {
            proposal.tags = scene.participant_ids;
            try {
                auto planted = foreshadowing_.plant(world_id, proposal);
                wrap.proposed_foreshadowing.push_back(planted);
            } catch (const std::exception& e) {
                spdlog::warn("foreshadowing proposal plant skipped: {}", e.what());
                wrap.warnings.push_back("伏笔提案写入失败: " + std::string(e.what()));
            }
        }
    }

    // Leak risk check
    wrap.leak_risks = secrets_.check_leak_risk(world_id, scene, final_markdown);

    // Chapter foreshadow stats
    wrap.chapter_foreshadow_stats =
        foreshadowing_.chapter_summary(world_id, scene.chapter_id);

    // 自动记忆压缩
    for (const auto& pid : scene.participant_ids) {
        try {
            int count = agents_.uncompressed_diary_count(pid);
            if (count >= compression_trigger_threshold_) {
                auto result = compact_agent_diaries(pid).get();
                if (result.compressed) {
                    wrap.compressed_memories.push_back(result.summary_id);
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("auto-compression failed for agent {}: {}", pid, e.what());
            wrap.warnings.push_back("角色 " + pid + " 记忆自动压缩失败");
        }
    }

    return wrap;
}

CharacterContextView
SceneOrchestrator::route_direct_message(const std::string& world_id,
                                         const std::string& target_agent_id,
                                         const std::string& message) const {
    ensure_world_exists(worlds_, world_id);

    CharacterContextView view;
    view.agent_id = target_agent_id;

    auto agent = agents_.get_agent(target_agent_id);
    if (!agent.has_value()) {
        view.system_prompt = "未知代理人: " + target_agent_id;
        return view;
    }

    if (agent->kind == AgentKind::Group) {
        // Group never speaks directly; select a member representative
        auto group = agents_.load_group(target_agent_id);
        if (!group.member_agent_ids.empty()) {
            // Deterministic selection by message content hash
            size_t idx = 0;
            for (char c : message) idx += static_cast<size_t>(c);
            idx = idx % group.member_agent_ids.size();
            auto member_id = group.member_agent_ids[idx];

            view.agent_id = member_id;
            try {
                auto card = agents_.load_character_card(member_id);
                std::ostringstream prompt;
                prompt << "# 群体代表发言\n";
                prompt << "群体: " << agent->name << "\n";
                prompt << "群体文化:\n" << group.culture_card_markdown << "\n\n";
                prompt << "你作为群体代表 " << card.name << " 发言:\n";
                prompt << card_to_prompt(card) << "\n";
                prompt << "## 用户消息\n" << message;
                view.system_prompt = prompt.str();

                // Load shared memory refs
                for (const auto& ref : group.shared_memory_ids) {
                    view.loaded_memory_refs.push_back(ref);
                }
            } catch (const std::exception& e) {
                spdlog::debug("group member card load({}) skipped: {}", member_id, e.what());
                view.system_prompt = "群体 " + agent->name + " 成员 " + member_id;
            }
        } else {
            view.system_prompt = "群体 " + agent->name + " 无可用成员";
        }
    } else {
        // Individual or Manager: use character card as system prompt core
        try {
            auto card = agents_.load_character_card(target_agent_id);
            std::ostringstream prompt;
            prompt << card_to_prompt(card) << "\n";
            prompt << "## 用户消息\n" << message;
            view.system_prompt = prompt.str();
        } catch (const std::exception& e) {
            spdlog::debug("individual card load({}) skipped: {}", target_agent_id, e.what());
            view.system_prompt = "代理人 " + target_agent_id + ":\n" + message;
        }
    }

    return view;
}

std::future<DiaryCompactionResult> SceneOrchestrator::compact_agent_diaries(const std::string& agent_id) {
    return std::async(std::launch::async, [this, agent_id]() -> DiaryCompactionResult {
        DiaryCompactionResult result;
        result.compressed = false;

        if (!compaction_llm_) {
            spdlog::warn("compact_agent_diaries: compaction_llm_ not set, skipping");
            return result;
        }

        auto diaries = agents_.uncompressed_diaries(agent_id, 20);
        if ((int)diaries.size() < compression_trigger_threshold_) {
            return result;
        }

        std::ostringstream prompt;
        prompt << "你是一个记忆管理助手。将以下" << diaries.size()
               << "条角色日记压缩为一份记忆摘要。\n\n";
        for (const auto& d : diaries) {
            prompt << "--- [" << d.world_time << "] 心情: " << d.mood << " ---\n";
            prompt << d.content << "\n\n";
        }
        prompt << "请以JSON格式输出摘要：\n"
               << R"({"summary":"以第三人称概述这段时间内角色的经历、情感变化、关系发展和重要发现，300字以内",)"
               << R"("key_events":["事件1","事件2"],)"
               << R"("emotional_arc":"从起始心情到结束心情的情感轨迹",)"
               << R"("relationship_changes":"关系变化概述，无变化则写'无明显变化'"})";

        ChatRequest req;
        req.model = compaction_model_.empty() ? "gpt-4o-mini" : compaction_model_;
        req.max_output_tokens = 1024;
        req.messages = {{"user", prompt.str()}};
        req.tools = {};
        req.enable_cache = false;

        auto llm_future = compaction_llm_->chat(req, [](StreamChunk){}, nullptr);
        auto llm_response = llm_future.get();

        if (token_counter_) {
            int prompt_tokens = token_counter_->count(prompt.str());
            int response_tokens = token_counter_->count(llm_response.text);
            spdlog::info("compact_agent_diaries for {}: {} diaries, prompt={} tokens, response={} tokens",
                agent_id, diaries.size(), prompt_tokens, response_tokens);
        }

        try {
            auto summary_json = nlohmann::json::parse(llm_response.text);

            MemorySummary summary;
            summary.id = make_id("summary");
            summary.agent_id = agent_id;
            summary.summary = summary_json.value("summary", llm_response.text);
            summary.period_start = diaries.front().world_time;
            summary.period_end = diaries.back().world_time;
            summary.created_at = now_iso_utc();
            for (const auto& d : diaries) summary.source_diary_ids.push_back(d.id);

            agents_.write_memory_summary(summary);
            result.summary_id = summary.id;
            result.compressed = true;
        } catch (...) {
            MemorySummary summary;
            summary.id = make_id("summary");
            summary.agent_id = agent_id;
            summary.summary = llm_response.text;
            summary.period_start = diaries.front().world_time;
            summary.period_end = diaries.back().world_time;
            summary.created_at = now_iso_utc();
            for (const auto& d : diaries) summary.source_diary_ids.push_back(d.id);

            agents_.write_memory_summary(summary);
            result.summary_id = summary.id;
            result.compressed = true;
        }

        return result;
    });
}

} // namespace merak::worldbuilding
