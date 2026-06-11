#pragma once

#include <merak/kg/kg_provider.hpp>
#include <merak/worldbuilding/world_models.hpp>

#include <string>
#include <vector>

namespace merak::worldbuilding {

class ExtractionService {
public:
    explicit ExtractionService(merak::kg::KnowledgeGraphProvider* kg_provider);

    std::string build_extraction_prompt(
        const std::string& scene_text,
        const std::vector<std::string>& participant_names) const;

    ExtractionResult process_llm_response(
        const std::string& llm_response,
        const std::string& world_id,
        const std::vector<std::string>& participant_agent_ids,
        const std::vector<std::string>& participant_names) const;

    ExtractionResult deduplicate(const ExtractionResult& raw) const;

private:
    std::vector<ExtractionCandidate> parse_llm_response(
        const std::string& response_json,
        const std::string& world_id,
        const std::vector<std::string>& participant_names) const;

    merak::kg::KnowledgeGraphProvider* kg_provider_;
};

} // namespace merak::worldbuilding
