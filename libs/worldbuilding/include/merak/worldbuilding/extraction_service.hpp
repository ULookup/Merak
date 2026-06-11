#pragma once

#include <merak/kg/kg_provider.hpp>
#include <merak/worldbuilding/world_models.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace merak { namespace llm { class LLMClient; }}

namespace merak::worldbuilding {

class ExtractionService {
public:
    struct Dependencies {
        std::shared_ptr<merak::llm::LLMClient> llm_client;
        merak::kg::KnowledgeGraphProvider* kg_provider;
    };

    explicit ExtractionService(Dependencies deps);

    ExtractionResult extract_from_scene(
        const std::string& scene_text,
        const std::string& world_id,
        const std::vector<std::string>& participant_agent_ids,
        const std::vector<std::string>& participant_names);

    ExtractionResult deduplicate(const ExtractionResult& raw) const;

private:
    std::string build_extraction_prompt(
        const std::string& scene_text,
        const std::vector<std::string>& participant_names) const;

    std::vector<ExtractionCandidate> parse_llm_response(
        const std::string& response_json,
        const std::string& world_id,
        const std::vector<std::string>& participant_names) const;

    Dependencies deps_;
};

} // namespace merak::worldbuilding
