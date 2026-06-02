#pragma once
#include <merak/tool_base.hpp>
#include <string>

namespace merak::tools {

class ReadFileTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ReadFileTool>(*this);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
};

class WriteFileTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<WriteFileTool>(*this);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
};

class EditFileTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<EditFileTool>(*this);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
};

class GlobTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<GlobTool>(*this);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
};

class GrepTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<GrepTool>(*this);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
};

class BashTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<BashTool>(*this);
    }
    bool is_concurrent_safe(const ToolCall& call) const override;
};

} // namespace merak::tools
