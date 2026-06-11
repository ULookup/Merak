#include <merak/worldbuilding/extraction_service.hpp>
#include <merak/worldbuilding/ids.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace merak::worldbuilding {

namespace {

// ─── helpers ───

static std::string strip_markdown_fences(std::string raw) {
    // Trim leading/trailing whitespace
    const auto start = raw.find_first_not_of(" \t\n\r");
    const auto end = raw.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return {};
    raw = raw.substr(start, end - start + 1);

    // Strip ```json / ``` wrapper if present
    if (raw.size() >= 7 && raw.substr(0, 7) == "```json") {
        raw = raw.substr(7);
        // Strip trailing ```
        auto pos = raw.rfind("```");
        if (pos != std::string::npos) raw = raw.substr(0, pos);
    } else if (raw.size() >= 3 && raw.substr(0, 3) == "```") {
        raw = raw.substr(3);
        auto pos = raw.rfind("```");
        if (pos != std::string::npos) raw = raw.substr(0, pos);
    }

    // Re-trim
    const auto s2 = raw.find_first_not_of(" \t\n\r");
    const auto e2 = raw.find_last_not_of(" \t\n\r");
    if (s2 == std::string::npos) return {};
    return raw.substr(s2, e2 - s2 + 1);
}

static std::string join_names(const std::vector<std::string>& names) {
    if (names.empty()) return "";
    std::ostringstream out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out << ", ";
        out << names[i];
    }
    return out.str();
}

} // namespace

// ─── Construction ───

ExtractionService::ExtractionService(merak::kg::KnowledgeGraphProvider* kg_provider)
    : kg_provider_(kg_provider) {}

// ─── Prompt builder ───

std::string ExtractionService::build_extraction_prompt(
    const std::string& scene_text,
    const std::vector<std::string>& participant_names) const {

    std::ostringstream prompt;
    prompt << R"(你是一个文本关系提取专家。请仔细阅读以下场景文本，提取其中角色之间的情感关系和互动关系。

## 场景文本
)";
    prompt << scene_text;
    prompt << R"(

## 参与角色
)";
    prompt << join_names(participant_names);
    prompt << R"(

## 提取要求
请分析场景中角色之间的互动，识别他们的关系。对于每一对存在互动的角色，提取关系信息。

请以严格的 JSON 数组格式返回结果。如果场景中没有明显的关系，返回空数组 []。

每个关系对象的字段如下：
- source_name: 关系源角色名称（必须在参与角色列表中）
- target_name: 关系目标角色名称（必须在参与角色列表中）
- kind_en: 关系类型的英文标识，可选值：acquaintance（认识）, friend（朋友）, lover（恋人）, kin（血缘）, master_apprentice（师徒）, superior_subordinate（上下级）, enemy（敌对）, rival（竞争）, ally（合作/盟友）, member（隶属）, guardian（守护）, benefactor（恩人）, grudge（仇人）, custom（自定义）。如果是自定义关系，额外填写 kind_custom 字段。
- kind_cn: 关系类型的中文描述
- a_to_b_stance: source 对 target 的态度，可选值：Friendly（友好）, Admiring（仰慕）, Dependent（依赖）, Neutral（中立）, Cautious（谨慎）, Distant（疏远）, Suspicious（怀疑）, Hostile（敌对）, Resentful（怨恨）, Fearful（恐惧）, Guilty（愧疚）, Disdainful（蔑视）, Unknown（未知）
- b_to_a_stance: target 对 source 的态度，可选值同上
- a_to_b_addressing: source 对 target 的称呼方式（如无则不填）
- b_to_a_addressing: target 对 source 的称呼方式（如无则不填）
- fact: 支撑该关系的关键事实，一句话概括
- description: 关系的详细描述

请只返回 JSON 数组，不要包含任何其他说明文字。
)";

    return prompt.str();
}

// ─── LLM response parser ───

std::vector<ExtractionCandidate>
ExtractionService::parse_llm_response(
    const std::string& response_json,
    const std::string& world_id,
    const std::vector<std::string>& participant_names) const {

    const auto cleaned = strip_markdown_fences(response_json);
    if (cleaned.empty()) return {};

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(cleaned);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Failed to parse LLM response as JSON: " +
                                 std::string(e.what()));
    }

    if (!parsed.is_array()) {
        throw std::runtime_error(
            "Expected JSON array in LLM response, got: " + cleaned.substr(0, 200));
    }

    std::vector<ExtractionCandidate> candidates;
    candidates.reserve(parsed.size());

    for (const auto& item : parsed) {
        ExtractionCandidate candidate;
        auto& rel = candidate.relation;

        rel.source_name = item.value("source_name", "");
        rel.target_name = item.value("target_name", "");
        rel.kind_en   = item.value("kind_en", "custom");
        rel.kind_cn   = item.value("kind_cn", "");
        rel.kind_custom = item.value("kind_custom", "");
        rel.fact      = item.value("fact", "");
        rel.description = item.value("description", "");
        rel.world_id  = world_id;
        rel.source_type = merak::kg::EntityType::Agent;
        rel.target_type = merak::kg::EntityType::Agent;

        // Auto-derive kind from string
        rel.kind = merak::kg::relation_kind_from_string(rel.kind_en);

        // Auto-derive kind_cn if not provided
        if (rel.kind_cn.empty() && rel.kind != merak::kg::RelationKind::Custom) {
            rel.kind_cn = merak::kg::relation_kind_cn(rel.kind);
        }

        // Parse stances
        if (item.contains("a_to_b_stance")) {
            rel.a_to_b_stance =
                merak::kg::stance_from_string(item["a_to_b_stance"].get<std::string>());
        } else {
            rel.a_to_b_stance = merak::kg::Stance::Unknown;
        }

        if (item.contains("b_to_a_stance")) {
            rel.b_to_a_stance =
                merak::kg::stance_from_string(item["b_to_a_stance"].get<std::string>());
        } else {
            rel.b_to_a_stance = merak::kg::Stance::Unknown;
        }

        // Optional addressing
        rel.a_to_b_addressing = item.value("a_to_b_addressing", "");
        rel.b_to_a_addressing = item.value("b_to_a_addressing", "");

        candidate.status = CandidateStatus::New;
        candidate.evidence = rel.fact;

        candidates.push_back(std::move(candidate));
    }

    return candidates;
}

// ─── Main extraction pipeline ───

ExtractionResult ExtractionService::process_llm_response(
    const std::string& llm_response,
    const std::string& world_id,
    const std::vector<std::string>& participant_agent_ids,
    const std::vector<std::string>& participant_names) {

    // Build name → agent_id map
    std::unordered_map<std::string, std::string> name_to_id;
    for (size_t i = 0; i < participant_names.size() && i < participant_agent_ids.size(); ++i) {
        name_to_id[participant_names[i]] = participant_agent_ids[i];
    }

    // Parse LLM response into candidates
    auto candidates = parse_llm_response(llm_response, world_id, participant_names);

    // Map source/target names to agent IDs and generate relation IDs
    for (auto& candidate : candidates) {
        auto& rel = candidate.relation;

        auto src_it = name_to_id.find(rel.source_name);
        if (src_it != name_to_id.end()) {
            rel.source_id = src_it->second;
        } else {
            rel.source_id = rel.source_name;
        }

        auto tgt_it = name_to_id.find(rel.target_name);
        if (tgt_it != name_to_id.end()) {
            rel.target_id = tgt_it->second;
        } else {
            rel.target_id = rel.target_name;
        }
    }

    // Build initial result
    ExtractionResult result;
    result.candidates = std::move(candidates);
    result.scene_id  = make_id("ext");
    result.world_id  = world_id;
    result.extraction_timestamp = now_iso_utc();

    // Deduplicate against existing KG
    return deduplicate(result);
}

// ─── Deduplication ───

ExtractionResult ExtractionService::deduplicate(
    const ExtractionResult& raw) const {

    ExtractionResult result = raw;

    for (auto& candidate : result.candidates) {
        const auto& rel = candidate.relation;

        // Query existing relations between the two entities
        merak::kg::SubGraph subgraph;
        try {
            subgraph = kg_provider_->query_subgraph(
                rel.world_id,
                {rel.source_name, rel.target_name},
                merak::kg::QueryFilters{});
        } catch (const std::exception&) {
            // If KG query fails, treat as new candidate
            candidate.status = CandidateStatus::New;
            continue;
        }

        // Find the best matching existing relation
        const merak::kg::GraphRelation* existing = nullptr;
        bool is_reversed = false;

        for (const auto& ex : subgraph.relations) {
            // Direct match
            if (ex.source_name == rel.source_name &&
                ex.target_name == rel.target_name) {
                existing = &ex;
                is_reversed = false;
                break;
            }
            // Reverse match (source and target swapped)
            if (ex.source_name == rel.target_name &&
                ex.target_name == rel.source_name) {
                existing = &ex;
                is_reversed = true;
                break;
            }
        }

        // No existing relation → new
        if (!existing) {
            candidate.status = CandidateStatus::New;
            continue;
        }

        candidate.existing = *existing;

        // Compare kind (different kind → conflict)
        // Check both the English label and, for custom relations, the custom label
        bool kind_differs = (rel.kind_en != existing->kind_en);
        if (!kind_differs && rel.kind_en == "custom" &&
            rel.kind_custom != existing->kind_custom) {
            kind_differs = true;
        }
        if (kind_differs) {
            candidate.status = CandidateStatus::Conflict;
            candidate.change_summary.push_back(
                "关系类型变化：" + existing->kind_cn + " → " + rel.kind_cn +
                " (" + existing->kind_en + " → " + rel.kind_en + ")");
            continue;
        }

        // Compare stances, accounting for possible reversed direction
        auto candidate_a2b = rel.a_to_b_stance;
        auto candidate_b2a = rel.b_to_a_stance;
        auto existing_a2b  = existing->a_to_b_stance;
        auto existing_b2a  = existing->b_to_a_stance;

        if (is_reversed) {
            std::swap(existing_a2b, existing_b2a);
        }

        bool stance_changed = false;
        if (candidate_a2b != merak::kg::Stance::Unknown &&
            existing_a2b != merak::kg::Stance::Unknown &&
            candidate_a2b != existing_a2b) {
            stance_changed = true;
            candidate.change_summary.push_back(
                rel.source_name + " 对 " + rel.target_name +
                " 的态度变化：" +
                merak::kg::to_string(existing_a2b) + " → " +
                merak::kg::to_string(candidate_a2b));
        }
        if (candidate_b2a != merak::kg::Stance::Unknown &&
            existing_b2a != merak::kg::Stance::Unknown &&
            candidate_b2a != existing_b2a) {
            stance_changed = true;
            candidate.change_summary.push_back(
                rel.target_name + " 对 " + rel.source_name +
                " 的态度变化：" +
                merak::kg::to_string(existing_b2a) + " → " +
                merak::kg::to_string(candidate_b2a));
        }

        if (stance_changed) {
            candidate.status = CandidateStatus::StanceChange;
            continue;
        }

        // Compare fact
        if (!rel.fact.empty() && rel.fact != existing->fact) {
            candidate.status = CandidateStatus::FactUpdate;
            candidate.change_summary.push_back("事实更新");
            continue;
        }

        // All matched → no change
        candidate.status = CandidateStatus::NoChange;
    }

    return result;
}

} // namespace merak::worldbuilding
