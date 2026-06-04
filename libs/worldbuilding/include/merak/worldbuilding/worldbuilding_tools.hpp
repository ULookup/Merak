#pragma once

#include <merak/tool_base.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>

#include <memory>
#include <string>
#include <vector>

namespace merak::worldbuilding {

struct ToolContext {
    std::string world_id;
    std::string scene_id;
    std::string caller_agent_id;
};

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
    DescribeCharacterTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<DescribeCharacterTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class SearchMyDiaryTool : public Tool {
public:
    SearchMyDiaryTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<SearchMyDiaryTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class LookAroundTool : public Tool {
public:
    LookAroundTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<LookAroundTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

// ====== Manager Tools ======

class QueryMapTool : public Tool {
public:
    QueryMapTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<QueryMapTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class QueryHistoryTool : public Tool {
public:
    QueryHistoryTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<QueryHistoryTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class QueryMagicTool : public Tool {
public:
    QueryMagicTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<QueryMagicTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class QueryFactionTool : public Tool {
public:
    QueryFactionTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<QueryFactionTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

// ====== God Read Tools ======

class ReadCharacterCardTool : public Tool {
public:
    ReadCharacterCardTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ReadCharacterCardTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class ReadSecretTool : public Tool {
public:
    ReadSecretTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ReadSecretTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class ReadForeshadowingTool : public Tool {
public:
    ReadForeshadowingTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ReadForeshadowingTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class ListOpenForeshadowingTool : public Tool {
public:
    ListOpenForeshadowingTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ListOpenForeshadowingTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

// ====== God Mutation Tools ======

class AdvanceWorldTimeTool : public Tool {
public:
    AdvanceWorldTimeTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<AdvanceWorldTimeTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class CreateCharacterTool : public Tool {
public:
    CreateCharacterTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<CreateCharacterTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class PlantForeshadowingTool : public Tool {
public:
    PlantForeshadowingTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<PlantForeshadowingTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class ExposeSecretTool : public Tool {
public:
    ExposeSecretTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ExposeSecretTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

class EndSceneTool : public Tool {
public:
    EndSceneTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<EndSceneTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};

// ====== WorldbuildingTools Factory ======

class WorldbuildingTools {
public:
    explicit WorldbuildingTools(WorldbuildingService& service)
        : service_(&service) {}

    std::vector<ToolSpec> specs_for(AgentKind kind) const;
    std::vector<std::unique_ptr<Tool>>
    create_tools(AgentKind kind, const ToolContext& ctx);

private:
    WorldbuildingService* service_;
};

} // namespace merak::worldbuilding
