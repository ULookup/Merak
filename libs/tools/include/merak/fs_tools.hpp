#pragma once
#include <merak/tool_base.hpp>
#include <merak/edit_journal.hpp>
#include <string>
#include <vector>

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

class StrReplaceTool : public Tool {
public:
    explicit StrReplaceTool(EditJournal* journal = nullptr) : journal_(journal) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<StrReplaceTool>(journal_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    EditJournal* journal_;
};

using EditFileTool = StrReplaceTool;

class MultiEditTool : public Tool {
public:
    explicit MultiEditTool(EditJournal* journal = nullptr) : journal_(journal) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<MultiEditTool>(journal_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    EditJournal* journal_;
};

class DeleteFileTool : public Tool {
public:
    explicit DeleteFileTool(EditJournal* journal = nullptr) : journal_(journal) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<DeleteFileTool>(journal_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    EditJournal* journal_;
};

class ListDirTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<ListDirTool>(*this);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
};

} // namespace merak::tools
