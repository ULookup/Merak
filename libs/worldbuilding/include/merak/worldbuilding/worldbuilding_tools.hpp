#pragma once

#include <merak/tool_base.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>

#include <memory>
#include <string>
#include <vector>

namespace merak { class LlmProvider; }

namespace merak::worldbuilding {


enum class ToolErrorCode {
    NOT_FOUND,
    NO_PERMISSION,
    INVALID_ARGUMENT,
    CONFLICT,
    EMPTY_RESULT,
    INTERNAL
};

inline std::string error_code_str(ToolErrorCode c) {
    switch (c) {
    case ToolErrorCode::NOT_FOUND:        return "NOT_FOUND";
    case ToolErrorCode::NO_PERMISSION:    return "NO_PERMISSION";
    case ToolErrorCode::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case ToolErrorCode::CONFLICT:         return "CONFLICT";
    case ToolErrorCode::EMPTY_RESULT:     return "EMPTY_RESULT";
    case ToolErrorCode::INTERNAL:         return "INTERNAL";
    }
    return "INTERNAL";
}

inline std::string ok_response(const nlohmann::json& data) {
    return nlohmann::json{{"ok", true}, {"data", data}}.dump();
}

inline std::string error_response(ToolErrorCode code,
                                   const std::string& message,
                                   const std::string& detail = "") {
    nlohmann::json err{{"code", error_code_str(code)}, {"message", message}};
    if (!detail.empty()) err["detail"] = detail;
    return nlohmann::json{{"ok", false}, {"error", err}}.dump();
}

inline std::string partial_success(const nlohmann::json& data,
                                    const nlohmann::json& warnings) {
    return nlohmann::json{{"ok", true}, {"data", data}, {"warnings", warnings}}.dump();
}

inline std::string make_snippet(const std::string& text, size_t max_len = 100) {
    if (text.size() <= max_len) return text;
    return text.substr(0, max_len) + "...";
}

inline bool is_in_vector(const std::vector<std::string>& vec, const std::string& val) {
    return std::find(vec.begin(), vec.end(), val) != vec.end();
}

// ====== Character Tools ======

class DescribeCharacterTool : public Tool {
public:
    DescribeCharacterTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<DescribeCharacterTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class SearchMyDiaryTool : public Tool {
public:
    SearchMyDiaryTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<SearchMyDiaryTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class ReadDiaryEntryTool : public Tool {
public:
    ReadDiaryEntryTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ReadDiaryEntryTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class BrowseDiaryRangeTool : public Tool {
public:
    BrowseDiaryRangeTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<BrowseDiaryRangeTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class LookAroundTool : public Tool {
public:
    LookAroundTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<LookAroundTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

// ====== Manager Tools ======

class QueryMapTool : public Tool {
public:
    QueryMapTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<QueryMapTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class QueryHistoryTool : public Tool {
public:
    QueryHistoryTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<QueryHistoryTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class QueryMagicTool : public Tool {
public:
    QueryMagicTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<QueryMagicTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class QueryFactionTool : public Tool {
public:
    QueryFactionTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<QueryFactionTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

// ====== God Read Tools ======

class ReadCharacterCardTool : public Tool {
public:
    ReadCharacterCardTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ReadCharacterCardTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class ReadSecretTool : public Tool {
public:
    ReadSecretTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ReadSecretTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class ReadForeshadowingTool : public Tool {
public:
    ReadForeshadowingTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ReadForeshadowingTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class ListOpenForeshadowingTool : public Tool {
public:
    ListOpenForeshadowingTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ListOpenForeshadowingTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

// ====== God Mutation Tools ======

class AdvanceWorldTimeTool : public Tool {
public:
    AdvanceWorldTimeTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<AdvanceWorldTimeTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class CreateCharacterTool : public Tool {
public:
    CreateCharacterTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<CreateCharacterTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class CreateSceneTool : public Tool {
public:
    CreateSceneTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<CreateSceneTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class CreateChapterTool : public Tool {
public:
    CreateChapterTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<CreateChapterTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class CreateArcTool : public Tool {
public:
    CreateArcTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<CreateArcTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class CreateSecretTool : public Tool {
public:
    CreateSecretTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<CreateSecretTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class AddWorldKnowledgeTool : public Tool {
public:
    AddWorldKnowledgeTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<AddWorldKnowledgeTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class CreateLocationTool : public Tool {
public:
    CreateLocationTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<CreateLocationTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class PlantForeshadowingTool : public Tool {
public:
    PlantForeshadowingTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<PlantForeshadowingTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class ExposeSecretTool : public Tool {
public:
    ExposeSecretTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ExposeSecretTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class SearchAgentTool : public Tool {
public:
    SearchAgentTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<SearchAgentTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class QueryWorldTool : public Tool {
public:
    QueryWorldTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<QueryWorldTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class EndSceneTool : public Tool {
public:
    EndSceneTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<EndSceneTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class UpdateAgentPromptTool : public Tool {
public:
    UpdateAgentPromptTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<UpdateAgentPromptTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class UpdateCharacterCardTool : public Tool {
public:
    UpdateCharacterCardTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<UpdateCharacterCardTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class WriteMyDiaryTool : public Tool {
public:
    WriteMyDiaryTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<WriteMyDiaryTool>(*svc_);
    }
    PermissionLevel permission() const override { return PermissionLevel::safe; }
private:
    WorldbuildingService* svc_;
};

class CompressMyMemoryTool : public Tool {
public:
    CompressMyMemoryTool(WorldbuildingService& svc, std::shared_ptr<LlmProvider> llm,
                         int threshold, const std::string& model)
        : svc_(&svc), llm_(std::move(llm)),
          threshold_(threshold), model_(model) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<CompressMyMemoryTool>(*svc_, llm_, threshold_, model_);
    }
    PermissionLevel permission() const override { return PermissionLevel::safe; }
private:
    WorldbuildingService* svc_;
    std::shared_ptr<LlmProvider> llm_;
    int threshold_;
    std::string model_;
};

class AddRelationTool : public Tool {
public:
    AddRelationTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<AddRelationTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class UpdateForeshadowTool : public Tool {
public:
    UpdateForeshadowTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<UpdateForeshadowTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

class DelegateToWriterTool : public Tool {
public:
    DelegateToWriterTool(WorldbuildingService& svc,
                         std::shared_ptr<LlmProvider> llm,
                         std::string default_model = "claude-sonnet-4-6")
        : svc_(&svc), llm_(std::move(llm)),
          default_model_(std::move(default_model)) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<DelegateToWriterTool>(*svc_, llm_, default_model_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    std::shared_ptr<LlmProvider> llm_;
    std::string default_model_;
};

// ====== RelationManager Tools ======

class QuerySubgraphTool : public Tool {
public:
    QuerySubgraphTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<QuerySubgraphTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class ExpandGraphTool : public Tool {
public:
    ExpandGraphTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ExpandGraphTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class FindPathTool : public Tool {
public:
    FindPathTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<FindPathTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class CheckConsistencyTool : public Tool {
public:
    CheckConsistencyTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<CheckConsistencyTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class ExtractSceneRelationsTool : public Tool {
public:
    ExtractSceneRelationsTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ExtractSceneRelationsTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
};

class UpsertRelationTool : public Tool {
public:
    UpsertRelationTool(WorldbuildingService& svc)
        : svc_(&svc) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<UpsertRelationTool>(*svc_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
};

// ====== WorldbuildingTools Factory ======

class WorldbuildingTools {
public:
    WorldbuildingTools(WorldbuildingService& service,
                       std::shared_ptr<LlmProvider> llm = nullptr,
                       int compression_threshold = 20,
                       std::string diary_model = "",
                       std::string writer_model = "claude-sonnet-4-6")
        : service_(&service), llm_(std::move(llm)),
          compression_threshold_(compression_threshold),
          diary_model_(std::move(diary_model)),
          writer_model_(std::move(writer_model)) {}

    std::vector<ToolSpec> specs_for(AgentKind kind) const;
    std::vector<std::unique_ptr<Tool>>
    create_tools(AgentKind kind) const;

private:
    WorldbuildingService* service_;
    std::shared_ptr<LlmProvider> llm_;
    int compression_threshold_ = 20;
    std::string diary_model_;
    std::string writer_model_;
};

} // namespace merak::worldbuilding
