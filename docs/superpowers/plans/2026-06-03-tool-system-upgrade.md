# Merak 工具系统升级 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 6 个内置工具（read_file, write_file, edit_file, glob, grep, execute_bash）从单文件 god class 拆分为独立文件，参照 Astra 增强安全边界与能力，新增 list_dir, delete_file, multi_edit, edit_journal。

**Architecture:** 分 9 个 phase 渐进实现。每个 phase 结束时 `cmake --build build -j` 必须通过。Phase A 做 ToolResult 扩展 + EditJournal（纯加法），Phase B 拆分文件（结构不变），Phase C-H 各自增强一个工具组，Phase I 收尾集成。

**Tech Stack:** C++23, nlohmann::json, POSIX, std::filesystem, CMake.

---

## Spec Reference

本计划执行 `docs/superpowers/specs/2026-06-03-tool-system-upgrade-design.md`。交叉引用使用 spec 章节号。

---

# Phase A — 基础层（纯加法，不破坏现有 build）

**Branch:** `feat/tool-system-upgrade`

---

### Task A1: ToolResult 扩展

**Files:**
- Modify: `libs/core/include/merak/message.hpp:15-19`

- [ ] **Step 1: 添加新字段**

将 `ToolResult` 结构体：
```cpp
struct ToolResult {
    std::string call_id;
    std::string output;
    bool is_error = false;
};
```

替换为：
```cpp
struct ToolResult {
    std::string call_id;
    std::string output;
    bool is_error = false;
    bool truncated = false;
    int exit_code = 0;
    long duration_ms = 0;
    bool cached = false;
};
```

（spec §11）

- [ ] **Step 2: Build**

```bash
cd /home/icepop/Merak && cmake --build build -j 2>&1 | tail -10
```
Expected: clean（新字段有默认值，所有现有代码无需改动）

- [ ] **Step 3: Commit**

```bash
git add libs/core/include/merak/message.hpp
git commit -m "feat(core): add truncated/exit_code/duration_ms/cached to ToolResult"
```

---

### Task A2: 创建 EditJournal

**Files:**
- Create: `libs/tools/include/merak/edit_journal.hpp`
- Create: `libs/tools/src/edit_journal.cpp`

- [ ] **Step 1: 写头文件**

```cpp
// libs/tools/include/merak/edit_journal.hpp
#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace merak {

class EditJournal {
public:
    struct Entry {
        std::filesystem::path path;
        std::string before;    // empty if file was new
        std::string after;     // empty if file was deleted
        std::chrono::steady_clock::time_point time;
    };

    void record(const std::filesystem::path& path,
                const std::string& before,
                const std::string& after);

    const std::vector<Entry>& entries() const { return journal_; }
    bool rollback(size_t count = 1);
    void clear() { journal_.clear(); }

private:
    std::vector<Entry> journal_;
    static constexpr size_t kMaxEntries = 100;
};

} // namespace merak
```

- [ ] **Step 2: 写实现文件**

```cpp
// libs/tools/src/edit_journal.cpp
#include <merak/edit_journal.hpp>
#include <fstream>

namespace merak {

void EditJournal::record(const std::filesystem::path& path,
                         const std::string& before,
                         const std::string& after) {
    journal_.push_back({path, before, after, std::chrono::steady_clock::now()});
    if (journal_.size() > kMaxEntries) journal_.erase(journal_.begin());
}

bool EditJournal::rollback(size_t count) {
    if (count > journal_.size()) return false;
    for (size_t i = 0; i < count; ++i) {
        auto& entry = journal_[journal_.size() - 1 - i];
        if (entry.before.empty()) {
            std::filesystem::remove(entry.path);
        } else {
            std::ofstream out(entry.path);
            out << entry.before;
        }
    }
    journal_.resize(journal_.size() - count);
    return true;
}

} // namespace merak
```

(spec §9)

- [ ] **Step 3: 在 CMakeLists.txt 中添加 edit_journal.cpp**

编辑 `libs/tools/CMakeLists.txt`，将：
```cmake
add_library(merak-tools STATIC
    src/tool_registry.cpp
    src/builtin_tools.cpp
)
```

替换为：
```cmake
add_library(merak-tools STATIC
    src/edit_journal.cpp
    src/tool_registry.cpp
    src/builtin_tools.cpp
)
```

- [ ] **Step 4: Build & commit**

```bash
cd /home/icepop/Merak && cmake --build build -j 2>&1 | tail -5
```
Expected: clean

```bash
git add libs/tools/include/merak/edit_journal.hpp libs/tools/src/edit_journal.cpp libs/tools/CMakeLists.txt
git commit -m "feat(tools): add EditJournal for file edit undo support"
```

---

# Phase B — 文件拆分（结构重构，行为不变）

**Branch:** `feat/tool-system-upgrade`（继续同一分支）

---

### Task B1: 创建 fs_tools.hpp + fs_tools.cpp（提取文件操作工具）

**Files:**
- Create: `libs/tools/include/merak/fs_tools.hpp`
- Create: `libs/tools/src/fs_tools.cpp`

- [ ] **Step 1: 写 fs_tools.hpp**

```cpp
// libs/tools/include/merak/fs_tools.hpp
#pragma once
#include <merak/tool_base.hpp>
#include <merak/edit_journal.hpp>
#include <functional>
#include <string>
#include <vector>

namespace merak::tools {

// ——— ReadFileTool ———
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

// ——— WriteFileTool ———
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

// ——— StrReplaceTool ———
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

// 向后兼容别名
using EditFileTool = StrReplaceTool;

// ——— MultiEditTool ———
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

// ——— DeleteFileTool ———
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

// ——— ListDirTool ———
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
```

- [ ] **Step 2: 写 fs_tools.cpp — 从 builtin_tools.cpp 提取并增强 read_file/write_file/edit_file**

先复制现有 builtin_tools.cpp 中 ReadFileTool, WriteFileTool, EditFileTool 的实现到 fs_tools.cpp，保持行为不变。

```cpp
// libs/tools/src/fs_tools.cpp
#include <merak/fs_tools.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

namespace merak::tools {

// ========== ReadFile ==========

ToolSpec ReadFileTool::spec() const {
    ToolSpec s;
    s.name = "read_file";
    s.description = "Read the contents of a file at the given path";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to the file to read"}
        },
        "required": ["path"]
    })";
    return s;
}

std::future<ToolResult> ReadFileTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args["path"].get<std::string>();
            if (!fs::exists(path)) {
                result.is_error = true;
                result.output = "File not found: " + path;
                return result;
            }
            if (!fs::is_regular_file(path)) {
                result.is_error = true;
                result.output = "Not a regular file: " + path;
                return result;
            }
            std::ifstream f(path);
            if (!f.is_open()) {
                result.is_error = true;
                result.output = "Cannot open file: " + path;
                return result;
            }
            std::ostringstream oss;
            oss << f.rdbuf();
            result.output = oss.str();
            spdlog::debug("ReadFile: read {} bytes from {}", result.output.size(), path);
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("ReadFile error: ") + e.what();
        }
        return result;
    });
}

// ========== WriteFile ==========

ToolSpec WriteFileTool::spec() const {
    ToolSpec s;
    s.name = "write_file";
    s.description = "Write content to a file, creating it if it doesn't exist";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to write to"},
            "content": {"type": "string", "description": "Content to write"}
        },
        "required": ["path", "content"]
    })";
    return s;
}

std::future<ToolResult> WriteFileTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args["path"].get<std::string>();
            std::string content = args["content"].get<std::string>();
            fs::create_directories(fs::path(path).parent_path());
            std::ofstream f(path);
            if (!f.is_open()) {
                result.is_error = true;
                result.output = "Cannot write to file: " + path;
                return result;
            }
            f << content;
            f.close();
            result.output = "File written: " + path + " (" +
                std::to_string(content.size()) + " bytes)";
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("WriteFile error: ") + e.what();
        }
        return result;
    });
}

// ========== StrReplace (formerly EditFile) ==========

ToolSpec StrReplaceTool::spec() const {
    ToolSpec s;
    s.name = "str_replace";
    s.description = "Replace a string in a file. old_str must match exactly once";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "File to edit"},
            "old_str": {"type": "string", "description": "Text to find and replace"},
            "new_str": {"type": "string", "description": "Replacement text"},
            "replace_all": {"type": "boolean", "default": false},
            "dry_run": {"type": "boolean", "default": false}
        },
        "required": ["path", "old_str", "new_str"]
    })";
    return s;
}

std::future<ToolResult> StrReplaceTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [this, call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args["path"].get<std::string>();
            std::string old_str = args["old_str"].get<std::string>();
            std::string new_str = args["new_str"].get<std::string>();
            bool replace_all = args.value("replace_all", false);
            bool dry_run = args.value("dry_run", false);

            std::ifstream f_in(path);
            if (!f_in.is_open()) {
                result.is_error = true;
                result.output = "Cannot open file: " + path;
                return result;
            }
            std::ostringstream oss;
            oss << f_in.rdbuf();
            f_in.close();
            std::string content = oss.str();

            size_t pos = content.find(old_str);
            if (pos == std::string::npos) {
                result.is_error = true;
                result.output = "old_str not found in file. Ensure exact whitespace match.";
                return result;
            }
            if (!replace_all) {
                size_t pos2 = content.find(old_str, pos + 1);
                if (pos2 != std::string::npos) {
                    result.is_error = true;
                    result.output = "old_str matches multiple locations. "
                        "Provide a larger string with more surrounding context.";
                    return result;
                }
            }

            if (journal_) {
                journal_->record(path, content, "");
            }

            if (dry_run) {
                result.output = "Dry run: would replace " + std::to_string(replace_all ?
                    std::count_if(content.begin(), content.end(), [&](char) { return content.find(old_str) != std::string::npos; }) : 1) + " match(es)";
                return result;
            }

            if (replace_all) {
                size_t start = 0;
                while ((start = content.find(old_str, start)) != std::string::npos) {
                    content.replace(start, old_str.size(), new_str);
                    start += new_str.size();
                }
            } else {
                content.replace(pos, old_str.size(), new_str);
            }

            std::ofstream f_out(path);
            f_out << content;
            f_out.close();

            if (journal_) {
                journal_->record(path, "", content);
            }

            result.output = "Edit applied to " + path;
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("EditFile error: ") + e.what();
        }
        return result;
    });
}

// ========== MultiEdit (stub) ==========

ToolSpec MultiEditTool::spec() const {
    ToolSpec s;
    s.name = "multi_edit";
    s.description = "Apply multiple str_replace edits atomically to a file";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string"},
            "edits": {"type": "array", "items": {
                "type": "object", "properties": {
                    "old_str": {"type": "string"},
                    "new_str": {"type": "string"}
                }, "required": ["old_str", "new_str"]
            }}
        },
        "required": ["path", "edits"]
    })";
    return s;
}

std::future<ToolResult> MultiEditTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [this, call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args["path"].get<std::string>();
            auto edits = args["edits"];

            std::ifstream f_in(path);
            if (!f_in.is_open()) {
                result.is_error = true;
                result.output = "Cannot open file: " + path;
                return result;
            }
            std::ostringstream oss;
            oss << f_in.rdbuf();
            f_in.close();
            std::string content = oss.str();

            // Verify all edits apply cleanly (atomicity check)
            struct Edit { size_t pos; std::string old_str; std::string new_str; };
            std::vector<Edit> parsed;
            for (auto& e : edits) {
                std::string old_s = e["old_str"].get<std::string>();
                std::string new_s = e["new_str"].get<std::string>();
                size_t p = content.find(old_s);
                if (p == std::string::npos) {
                    result.is_error = true;
                    result.output = "Edit not found: '" + old_s + "'";
                    return result;
                }
                size_t p2 = content.find(old_s, p + 1);
                if (p2 != std::string::npos) {
                    result.is_error = true;
                    result.output = "old_str matches multiple locations: '" + old_s + "'";
                    return result;
                }
                parsed.push_back({p, old_s, new_s});
            }

            // Overlap check
            std::sort(parsed.begin(), parsed.end(), [](auto& a, auto& b) { return a.pos < b.pos; });
            for (size_t i = 1; i < parsed.size(); ++i) {
                if (parsed[i - 1].pos + parsed[i - 1].old_str.size() > parsed[i].pos) {
                    result.is_error = true;
                    result.output = "Edits overlap at offset " + std::to_string(parsed[i].pos);
                    return result;
                }
            }

            if (journal_) journal_->record(path, content, "");

            // Apply edits in reverse order to preserve positions
            for (int i = static_cast<int>(parsed.size()) - 1; i >= 0; --i) {
                content.replace(parsed[i].pos, parsed[i].old_str.size(), parsed[i].new_str);
            }

            std::ofstream f_out(path);
            f_out << content;
            f_out.close();

            if (journal_) journal_->record(path, "", content);

            result.output = std::to_string(edits.size()) + " edit(s) applied to " + path;
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("MultiEdit error: ") + e.what();
        }
        return result;
    });
}

// ========== DeleteFile ==========

ToolSpec DeleteFileTool::spec() const {
    ToolSpec s;
    s.name = "delete_file";
    s.description = "Delete a file. Refuses .git/ contents and directories.";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "File to delete"},
            "explanation": {"type": "string", "description": "Why this file is being deleted"}
        },
        "required": ["path"]
    })";
    return s;
}

std::future<ToolResult> DeleteFileTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [this, call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args["path"].get<std::string>();
            fs::path p(path);
            auto canon = fs::canonical(fs::absolute(p));

            if (canon.string().find("/.git/") != std::string::npos ||
                canon.filename() == ".git") {
                result.is_error = true;
                result.output = "Refusing to delete .git/ contents: " + path;
                return result;
            }
            if (fs::is_directory(canon)) {
                result.is_error = true;
                result.output = "Refusing to delete directory: " + path;
                return result;
            }
            if (!fs::exists(canon)) {
                result.output = "File not found: " + path;
                return result;
            }

            if (journal_) {
                std::ifstream in(canon);
                std::ostringstream oss;
                oss << in.rdbuf();
                journal_->record(canon, oss.str(), "");
            }

            fs::remove(canon);
            result.output = "Deleted: " + path;
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("DeleteFile error: ") + e.what();
        }
        return result;
    });
}

// ========== ListDir ==========

ToolSpec ListDirTool::spec() const {
    ToolSpec s;
    s.name = "list_dir";
    s.description = "List files and directories, recursively up to depth 10";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Directory to list, default '.'"},
            "depth": {"type": "integer", "default": 1, "maximum": 10},
            "show_hidden": {"type": "boolean", "default": false}
        }
    })";
    return s;
}

static const std::vector<std::string> kSkipDirs = {
    ".git", "node_modules", "target", "__pycache__", "build", ".venv", "venv"
};

std::future<ToolResult> ListDirTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args.value("path", ".");
            int depth = args.value("depth", 1);
            bool show_hidden = args.value("show_hidden", false);
            if (depth < 1) depth = 1;
            if (depth > 10) depth = 10;

            fs::path root(path);
            if (!fs::exists(root)) {
                result.is_error = true;
                result.output = "Directory not found: " + path;
                return result;
            }
            if (!fs::is_directory(root)) {
                result.is_error = true;
                result.output = "Not a directory: " + path;
                return result;
            }

            nlohmann::json entries = nlohmann::json::array();
            std::set<std::string> seen;
            int count = 0;

            auto walk = [&](auto& self, const fs::path& dir, int current_depth) -> void {
                if (current_depth > depth || count >= 500) return;
                for (auto& entry : fs::directory_iterator(dir)) {
                    if (count >= 500) break;
                    std::string name = entry.path().filename().string();
                    if (!show_hidden && name.starts_with(".") && name != ".") continue;
                    std::string type = entry.is_directory() ? "dir" : "file";
                    uintmax_t sz = entry.is_regular_file() ? entry.file_size() : 0;

                    entries.push_back({
                        {"name", name},
                        {"type", type},
                        {"size", sz}
                    });
                    count++;

                    if (entry.is_directory() && current_depth < depth) {
                        bool skip = false;
                        for (auto& sd : kSkipDirs) {
                            if (name == sd) { skip = true; break; }
                        }
                        if (!skip) self(self, entry.path(), current_depth + 1);
                    }
                }
            };

            walk(walk, root, 1);

            result.output = entries.dump();
            result.truncated = count >= 500;
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("ListDir error: ") + e.what();
        }
        return result;
    });
}

} // namespace merak::tools
```

- [ ] **Step 3: Build**

```bash
cd /home/icepop/Merak && cmake --build build -j 2>&1 | tail -15
```
Expected: 编译错误 — `fs_tools.cpp` 未加入 CMakeLists.txt。先验证头文件无语法错误：
```bash
c++ -std=c++23 -fsyntax-only -Ilibs/tools/include -Ilibs/core/include -Ilibs/mcp/include -Ilibs/config/include -I$(find build -name "nlohmann" -type d 2>/dev/null | head -1)/.. libs/tools/src/fs_tools.cpp 2>&1 | head -20
```

如果 spdlog/nlohmann 找不到路径，跳过 syntax check，在 Task B5 统一编译验证。

- [ ] **Step 4: Commit（暂不编译验证，等 Phase B 全部文件就位再 build）**

```bash
git add libs/tools/include/merak/fs_tools.hpp libs/tools/src/fs_tools.cpp
git commit -m "feat(tools): create fs_tools with ReadFile/WriteFile/StrReplace/MultiEdit/DeleteFile/ListDir"
```

---

### Task B2: 创建 search_tools.hpp + search_tools.cpp（提取搜索工具）

**Files:**
- Create: `libs/tools/include/merak/search_tools.hpp`
- Create: `libs/tools/src/search_tools.cpp`

- [ ] **Step 1: 写 search_tools.hpp**

```cpp
// libs/tools/include/merak/search_tools.hpp
#pragma once
#include <merak/tool_base.hpp>
#include <string>

namespace merak::tools {

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

} // namespace merak::tools
```

- [ ] **Step 2: 写 search_tools.cpp — 从 builtin_tools.cpp 复制并修复 GlobTool 和 GrepTool**

GlobTool：改用 `<glob.h>` 系统调用
GrepTool：去掉文件扩展名限制，增加 include/exclude 参数

```cpp
// libs/tools/src/search_tools.cpp
#include <merak/search_tools.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <glob.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <set>

namespace fs = std::filesystem;

namespace merak::tools {

// ========== Glob (uses system glob(3)) ==========

ToolSpec GlobTool::spec() const {
    ToolSpec s;
    s.name = "glob";
    s.description = "Find files matching a glob pattern (e.g. **/*.cpp)";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "pattern": {"type": "string", "description": "Glob pattern to match"}
        },
        "required": ["pattern"]
    })";
    return s;
}

std::future<ToolResult> GlobTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string pattern = args["pattern"].get<std::string>();

            glob_t g{};
            int rc = glob(pattern.c_str(), GLOB_NOSORT | GLOB_BRACE, nullptr, &g);
            if (rc == GLOB_NOMATCH || g.gl_pathc == 0) {
                result.output = "No files matched";
                globfree(&g);
                return result;
            }

            std::vector<std::string> matches;
            for (size_t i = 0; i < g.gl_pathc && matches.size() < 100; ++i) {
                std::string p = g.gl_pathv[i];
                if (fs::exists(p)) matches.push_back(p);
            }
            globfree(&g);

            std::sort(matches.begin(), matches.end());
            matches.erase(std::unique(matches.begin(), matches.end()), matches.end());

            if (matches.empty()) {
                result.output = "No files matched";
            } else {
                std::ostringstream oss;
                for (auto& m : matches) oss << m << "\n";
                std::string out = oss.str();
                if (out.size() > 100000) { out.resize(100000); result.truncated = true; }
                result.output = out;
            }
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("Glob error: ") + e.what();
        }
        return result;
    });
}

// ========== Grep (fixed: no extension filter, added include/exclude) ==========

ToolSpec GrepTool::spec() const {
    ToolSpec s;
    s.name = "grep";
    s.description = "Search for a regex pattern in files";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "pattern": {"type": "string", "description": "Regex pattern to search for"},
            "path": {"type": "string", "description": "File or directory to search in", "default": "."},
            "include": {"type": "string", "description": "Glob pattern to filter files, e.g. '*.cpp'"},
            "exclude": {"type": "string", "description": "Glob pattern to exclude, e.g. 'build/**'"}
        },
        "required": ["pattern"]
    })";
    return s;
}

static bool is_text_file(const fs::path& p) {
    std::ifstream f(p);
    if (!f.is_open()) return false;
    char buf[1024];
    f.read(buf, sizeof(buf));
    auto n = f.gcount();
    for (std::streamsize i = 0; i < n; ++i) {
        if (buf[i] == '\0') return false;
    }
    return true;
}

std::future<ToolResult> GrepTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string pattern = args["pattern"].get<std::string>();
            std::string path_str = args.value("path", ".");
            std::string include = args.value("include", "");
            std::string exclude = args.value("exclude", "");

            std::regex re(pattern, std::regex::ECMAScript);
            std::ostringstream output;
            int match_count = 0;

            auto search_file = [&](const fs::path& p) {
                if (match_count >= 50) return;
                if (!is_text_file(p)) return;
                std::ifstream f(p);
                if (!f.is_open()) return;
                std::string line;
                int lineno = 0;
                while (std::getline(f, line) && match_count < 50) {
                    lineno++;
                    if (std::regex_search(line, re)) {
                        output << p.string() << ":" << lineno << ":" << line << "\n";
                        match_count++;
                    }
                }
            };

            auto matches_filter = [&](const fs::path& p) -> bool {
                std::string s = p.string();
                if (!exclude.empty()) {
                    glob_t ge{};
                    int rc = glob(exclude.c_str(), GLOB_NOSORT, nullptr, &ge);
                    bool excl = false;
                    for (size_t i = 0; i < ge.gl_pathc; ++i) {
                        if (s.find(ge.gl_pathv[i]) != std::string::npos) { excl = true; break; }
                    }
                    globfree(&ge);
                    if (excl) return false;
                }
                if (!include.empty()) {
                    glob_t gi{};
                    int rc = glob(include.c_str(), GLOB_NOSORT, nullptr, &gi);
                    bool incl = false;
                    for (size_t i = 0; i < gi.gl_pathc; ++i) {
                        if (s.find(gi.gl_pathv[i]) != std::string::npos) { incl = true; break; }
                    }
                    globfree(&gi);
                    if (!incl) return false;
                }
                return true;
            };

            fs::path root(path_str);
            if (fs::is_regular_file(root)) {
                search_file(root);
            } else if (fs::is_directory(root)) {
                for (auto& entry : fs::recursive_directory_iterator(root)) {
                    if (match_count >= 50) break;
                    if (!entry.is_regular_file()) continue;
                    if (!matches_filter(entry.path())) continue;
                    search_file(entry.path());
                }
            }

            std::string out = output.str();
            if (out.size() > 10000) { out.resize(10000); result.truncated = true; }
            result.output = out.empty() ? "No matches found" : out;
        } catch (const std::regex_error& e) {
            result.is_error = true;
            result.output = std::string("Regex error: ") + e.what();
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("Grep error: ") + e.what();
        }
        return result;
    });
}

} // namespace merak::tools
```

- [ ] **Step 3: Commit**

```bash
git add libs/tools/include/merak/search_tools.hpp libs/tools/src/search_tools.cpp
git commit -m "feat(tools): create search_tools with fixed Glog and Grep"
```

---

### Task B3: 创建 shell_tool.hpp + shell_tool.cpp（提取 bash 工具并升级安全检查）

**Files:**
- Create: `libs/tools/include/merak/shell_tool.hpp`
- Create: `libs/tools/src/shell_tool.cpp`

- [ ] **Step 1: 写 shell_tool.hpp**

```cpp
// libs/tools/include/merak/shell_tool.hpp
#pragma once
#include <merak/tool_base.hpp>
#include <string>

namespace merak::tools {

class BashTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<BashTool>(*this);
    }
    bool is_concurrent_safe(const ToolCall& call) const override;

private:
    static bool check_dangerous(const std::string& command);
    static bool is_safe_readonly(const std::string& command);
    static bool check_git_destructive(const std::string& command);
    static bool check_sql_destructive(const std::string& command);
};

} // namespace merak::tools
```

- [ ] **Step 2: 写 shell_tool.cpp — 复制 BashTool 实现并重构**

完整实现移入，安全检查升级为 5 层：

```cpp
// libs/tools/src/shell_tool.cpp
#include <merak/shell_tool.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <array>
#include <cstdio>
#include <chrono>
#include <future>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>

namespace merak::tools {

// Layer 1: Dangerous command patterns
static const std::vector<std::string> kDangerousPatterns = {
    "rm -rf /", "mkfs.", "dd if=", ":(){ :|:& };:", "> /dev/sda",
    "chmod 777 /", "curl | sudo", "base64 -d | sh", "curl | bash",
    "wget | sh", "chmod -R 777 /", "> /etc/", "fdisk", "parted",
};

// Layer 2: Irreversible git operations
static const std::regex kGitPushForce(R"(git\s+push\s+.*(--force|--force-with-lease).*(main|master))");
static const std::regex kGitResetHard(R"(git\s+reset\s+--hard)");
static const std::regex kGitCleanFdx(R"(git\s+clean\s+-fdx)");

// Layer 3: Variable indirection bypass — detect dangerous words inside $() or backticks
static const std::regex kSubshellBypass(R"(\$\(.*(rm|mkfs|dd|chmod|>|sudo)\b)");
static const std::regex kBacktickBypass(R"(`.*(rm|mkfs|dd|chmod|>|sudo)\b`)");

// Layer 4: SQL destruction
static const std::regex kSqlDrop(R"(\bDROP\s+(TABLE|DATABASE|SCHEMA)\b)", std::regex::icase);
static const std::regex kSqlTruncate(R"(\bTRUNCATE\s+TABLE\b)", std::regex::icase);
static const std::regex kSqlDeleteNoWhere(R"(\bDELETE\s+FROM\b.*(?!.*\bWHERE\b))", std::regex::icase);

bool BashTool::check_dangerous(const std::string& command) {
    // Layer 1: Pattern blacklist
    for (auto& p : kDangerousPatterns) {
        if (command.find(p) != std::string::npos) return true;
    }
    // Layer 2: Git destructive
    if (std::regex_search(command, kGitPushForce)) return true;
    if (std::regex_search(command, kGitResetHard)) return true;
    if (std::regex_search(command, kGitCleanFdx)) return true;
    // Layer 3: Variable bypass
    if (std::regex_search(command, kSubshellBypass)) return true;
    if (std::regex_search(command, kBacktickBypass)) return true;
    // Layer 4: SQL destruction
    if (std::regex_search(command, kSqlDrop)) return true;
    if (std::regex_search(command, kSqlTruncate)) return true;
    if (std::regex_search(command, kSqlDeleteNoWhere)) return true;
    return false;
}

bool BashTool::check_git_destructive(const std::string& command) {
    return std::regex_search(command, kGitPushForce)
        || std::regex_search(command, kGitResetHard)
        || std::regex_search(command, kGitCleanFdx);
}

bool BashTool::check_sql_destructive(const std::string& command) {
    return std::regex_search(command, kSqlDrop)
        || std::regex_search(command, kSqlTruncate)
        || std::regex_search(command, kSqlDeleteNoWhere);
}

bool BashTool::is_safe_readonly(const std::string& cmd) {
    static const std::vector<std::string> safe = {
        "ls ", "find ", "cat ", "head ", "tail ", "wc ", "du ", "stat ", "file ", "pwd"
    };
    for (auto& p : safe) {
        if (cmd.rfind(p, 0) == 0) return true;
    }
    return false;
}

bool BashTool::is_concurrent_safe(const ToolCall& call) const {
    try {
        auto args = nlohmann::json::parse(call.arguments);
        std::string cmd = args["command"].get<std::string>();
        return is_safe_readonly(cmd);
    } catch (...) { return false; }
}

ToolSpec BashTool::spec() const {
    ToolSpec s;
    s.name = "execute_bash";
    s.description = "Execute a bash command and return its output";
    s.source = "builtin";
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "command": {"type": "string", "description": "Bash command to execute"},
            "timeout_ms": {"type": "integer", "description": "Max time before kill (default 30000)"}
        },
        "required": ["command"]
    })JSON";
    return s;
}

std::future<ToolResult> BashTool::execute(ToolCall call, ToolExecutionContext context) {
    return std::async(std::launch::async, [call = std::move(call), context]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        auto start = std::chrono::steady_clock::now();

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string command = args["command"].get<std::string>();
            int timeout_ms = args.value("timeout_ms", 30000);

            if (check_dangerous(command)) {
                result.is_error = true;
                result.output = "Dangerous command rejected";
                return result;
            }

            // Layer 5: sandbox path check — all writes must be within cwd
            std::string cwd = std::filesystem::current_path().string();
            // Simple heuristic: check for redirected output outside cwd
            std::regex redirect_out(R"(\s>+\s*(/[^\s]+))");
            std::smatch m;
            if (std::regex_search(command, m, redirect_out)) {
                std::string target = m[1].str();
                if (!target.starts_with(cwd) && !target.starts_with("/dev/null")
                    && !target.starts_with("/tmp")) {
                    result.is_error = true;
                    result.output = "Output redirect outside project: " + target;
                    return result;
                }
            }

            // Read-only dedup cache (only for safe commands)
            if (is_safe_readonly(command)) {
                struct CacheEntry {
                    std::string output; int exit_code;
                    std::chrono::steady_clock::time_point timestamp;
                };
                static std::map<std::string, CacheEntry> ro_cache;
                static std::mutex cache_mutex;
                static constexpr int kCacheTTL = 60;

                {
                    std::lock_guard<std::mutex> lock(cache_mutex);
                    auto it = ro_cache.find(command);
                    if (it != ro_cache.end()) {
                        auto age = std::chrono::steady_clock::now() - it->second.timestamp;
                        if (age < std::chrono::seconds(kCacheTTL)) {
                            result.output = it->second.output + "\n[cached]";
                            result.exit_code = it->second.exit_code;
                            result.cached = true;
                            auto end = std::chrono::steady_clock::now();
                            result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                            return result;
                        }
                    }
                }
            }

            // Fork and exec
            int pipefd[2];
            if (pipe(pipefd) < 0) {
                result.is_error = true;
                result.output = "Failed to create pipe";
                return result;
            }

            pid_t pid = fork();
            if (pid < 0) {
                close(pipefd[0]); close(pipefd[1]);
                result.is_error = true;
                result.output = "Failed to fork";
                return result;
            }

            if (pid == 0) {
                setpgid(0, 0);
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
                _exit(127);
            }

            close(pipefd[1]);
            setpgid(pid, pid);
            std::array<char, 65536> buf{};
            std::string output;

            auto deadline = start + std::chrono::milliseconds(timeout_ms);

            while (true) {
                if (context.cancellation && context.cancellation->cancelled()) {
                    kill(-pid, SIGKILL);
                    waitpid(pid, nullptr, 0);
                    result.is_error = true;
                    result.output = output + "\n[Cancelled]";
                    close(pipefd[0]);
                    return result;
                }
                auto now = std::chrono::steady_clock::now();
                if (now >= deadline) {
                    kill(pid, SIGKILL);
                    waitpid(pid, nullptr, 0);
                    result.is_error = true;
                    result.output = output + "\n[Timeout after " + std::to_string(timeout_ms) + "ms]";
                    close(pipefd[0]);
                    return result;
                }

                int remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(pipefd[0], &fds);
                struct timeval tv = {remaining_ms / 1000, (remaining_ms % 1000) * 1000};

                int ret = select(pipefd[0] + 1, &fds, nullptr, nullptr, &tv);
                if (ret > 0) {
                    ssize_t n = read(pipefd[0], buf.data(), buf.size() - 1);
                    if (n > 0) {
                        buf[n] = '\0';
                        output += buf.data();
                    } else { break; }
                } else if (ret == 0) { continue; }
                else { break; }
            }

            close(pipefd[0]);
            int status;
            waitpid(pid, &status, 0);
            result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

            // exit code semantics
            bool is_error = false;
            if (result.exit_code != 0) {
                static const std::regex kOkNonZero(R"(^\s*(grep|rg|diff|find)\b)");
                if (std::regex_search(command, kOkNonZero)) {
                    if (result.exit_code == 1) {
                        // grep/diff/find exit 1 = no matches/differences — not an error
                    } else {
                        is_error = true;
                    }
                } else {
                    is_error = true;
                }
            }

            result.is_error = is_error;
            result.output = is_error
                ? output + "\n[exit code: " + std::to_string(result.exit_code) + "]"
                : (output.empty() ? "(no output)" : output);

            auto end = std::chrono::steady_clock::now();
            result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("Bash error: ") + e.what();
        }
        return result;
    });
}

} // namespace merak::tools
```

- [ ] **Step 3: Commit**

```bash
git add libs/tools/include/merak/shell_tool.hpp libs/tools/src/shell_tool.cpp
git commit -m "feat(tools): create shell_tool with 5-layer bash safety check"
```

---

### Task B4: 更新 builtin_tools.hpp 为聚合头文件

**Files:**
- Modify: `libs/tools/include/merak/builtin_tools.hpp`

- [ ] **Step 1: 重写 builtin_tools.hpp**

将 `builtin_tools.hpp` 的所有类声明替换为 include 聚合：

```cpp
// libs/tools/include/merak/builtin_tools.hpp
#pragma once

// Aggregation header — includes all builtin tool types.
// Individual tool implementations live in their respective files:
//   fs_tools.hpp, search_tools.hpp, shell_tool.hpp

#include <merak/fs_tools.hpp>
#include <merak/search_tools.hpp>
#include <merak/shell_tool.hpp>
```

- [ ] **Step 2: Commit**

```bash
git add libs/tools/include/merak/builtin_tools.hpp
git commit -m "refactor(tools): convert builtin_tools.hpp to aggregation header"
```

---

### Task B5: 更新 CMakeLists.txt 并验证编译

**Files:**
- Modify: `libs/tools/CMakeLists.txt`
- Delete: `libs/tools/src/builtin_tools.cpp`（实现已迁移到 fs_tools.cpp, search_tools.cpp, shell_tool.cpp）

- [ ] **Step 1: 更新 CMakeLists.txt**

将：
```cmake
add_library(merak-tools STATIC
    src/edit_journal.cpp
    src/tool_registry.cpp
    src/builtin_tools.cpp
)
```

替换为：
```cmake
add_library(merak-tools STATIC
    src/edit_journal.cpp
    src/tool_registry.cpp
    src/fs_tools.cpp
    src/search_tools.cpp
    src/shell_tool.cpp
)
```

- [ ] **Step 2: 删除 builtin_tools.cpp**

```bash
rm libs/tools/src/builtin_tools.cpp
```

- [ ] **Step 3: Build**

```bash
cd /home/icepop/Merak && cmake --build build -j 2>&1 | tail -20
```

Expected: 会有编译错误 — `main.cpp` 中 `tools::EditFileTool` 引用的是 class name，而 `StrReplaceTool` 现在是别名 `using EditFileTool = StrReplaceTool`。需要修复 main.cpp。

修复 `cli/src/main.cpp:67`：

将：
```cpp
tools->register_tool(std::make_unique<tools::EditFileTool>());
```
替换为：
```cpp
tools->register_tool(std::make_unique<tools::StrReplaceTool>());
```

同时新增其他工具注册。在 Phase I 统一处理。

- [ ] **Step 4: 处理其他编译错误**

检查所有 `tools::EditFileTool` 引用并替换为 `tools::StrReplaceTool`：

```bash
grep -rn "EditFileTool" /home/icepop/Merak --include="*.cpp" --include="*.hpp" 2>/dev/null | grep -v build | grep -v ".git/"
```

Expected 结果：只有 test_tools.cpp 和 main.cpp 有引用。修改 test_tools.cpp 中相应引用（如果存在）。

- [ ] **Step 5: Build until clean**

```bash
cd /home/icepop/Merak && cmake --build build -j 2>&1 | tail -5
```
Expected: clean

- [ ] **Step 6: Commit**

```bash
git add libs/tools/CMakeLists.txt cli/src/main.cpp libs/tools/tests/test_tools.cpp
git rm libs/tools/src/builtin_tools.cpp
git commit -m "refactor(tools): update CMakeLists for split tool files, remove builtin_tools.cpp"
```

---

# Phase C — read_file 增强

### Task C1: read_file 增加二进制检测、偏移/行数限制、outline 降级、图片处理

**Files:**
- Modify: `libs/tools/src/fs_tools.cpp`（ReadFileTool::spec() 和 execute()）

**由于 Task C1-C2 代码量较大，分两步 commit。**

- [ ] **Step 1: 更新 spec() — 增加参数**

将 ReadFileTool::spec() 中的 parameters_json 替换为：

```cpp
ToolSpec ReadFileTool::spec() const {
    ToolSpec s;
    s.name = "read_file";
    s.description = "Read file contents with optional offset/limit and outline support";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to the file to read"},
            "offset": {"type": "integer", "description": "Start line (1-based)"},
            "limit": {"type": "integer", "description": "Number of lines to read"},
            "outline": {"type": "boolean", "description": "Return a structural outline instead of full content"}
        },
        "required": ["path"]
    })";
    return s;
}
```

- [ ] **Step 2: 重写 execute() 方法**

```cpp
std::future<ToolResult> ReadFileTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args["path"].get<std::string>();
            int offset = args.value("offset", 0);
            int limit = args.value("limit", 0);
            bool outline = args.value("outline", false);

            if (!fs::exists(path)) {
                result.is_error = true;
                result.output = "File not found: " + path;
                return result;
            }
            if (!fs::is_regular_file(path)) {
                result.is_error = true;
                result.output = "Not a regular file: " + path;
                return result;
            }

            // Image detection
            std::string ext = fs::path(path).extension().string();
            static const std::set<std::string> kImageExt = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp"};
            if (kImageExt.count(ext)) {
                auto sz = fs::file_size(path);
                if (sz > 1.5 * 1024 * 1024) {
                    result.is_error = true;
                    result.output = "Image too large (" + std::to_string(sz) + " bytes, max 1.5MB)";
                    return result;
                }
                std::ifstream f(path, std::ios::binary);
                std::ostringstream oss;
                oss << f.rdbuf();
                std::string data = oss.str();
                // base64 encode
                static const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                std::string b64;
                b64.reserve(((data.size() + 2) / 3) * 4);
                for (size_t i = 0; i < data.size(); i += 3) {
                    uint32_t n = static_cast<unsigned char>(data[i]) << 16;
                    if (i + 1 < data.size()) n |= static_cast<unsigned char>(data[i+1]) << 8;
                    if (i + 2 < data.size()) n |= static_cast<unsigned char>(data[i+2]);
                    b64 += kBase64[(n >> 18) & 63];
                    b64 += kBase64[(n >> 12) & 63];
                    b64 += kBase64[(n >> 6) & 63];
                    b64 += kBase64[n & 63];
                }
                if (data.size() % 3) { b64[b64.size() - 1] = '='; }
                if (data.size() % 3 == 1) { b64[b64.size() - 2] = '='; }
                result.output = nlohmann::json{
                    {"type", "image"},
                    {"mime", "image/" + ext.substr(1)},
                    {"data", "data:image/" + ext.substr(1) + ";base64," + b64},
                    {"size_bytes", data.size()}
                }.dump();
                spdlog::debug("ReadFile: returned base64 image from {}", path);
                return result;
            }

            // Read content
            std::ifstream f(path);
            if (!f.is_open()) {
                result.is_error = true;
                result.output = "Cannot open file: " + path;
                return result;
            }
            std::ostringstream oss;
            oss << f.rdbuf();
            std::string content = oss.str();

            // Binary detection
            if (!content.empty()) {
                auto first_nul = content.find('\0');
                if (first_nul != std::string::npos && first_nul < 1024) {
                    result.is_error = true;
                    result.output = "Binary file detected. Cannot display.";
                    return result;
                }
            }

            // Outline mode
            if (outline) {
                std::istringstream iss(content);
                std::string line;
                int lineno = 0;
                nlohmann::json outline_json = nlohmann::json::array();
                while (std::getline(iss, line)) {
                    lineno++;
                    // Extract leading non-space content for keyword matching
                    std::string trimmed = line;
                    size_t start = trimmed.find_first_not_of(" \t");
                    if (start == std::string::npos) continue;
                    trimmed = trimmed.substr(start);
                    if (trimmed.starts_with("#") || trimmed.starts_with("class ") ||
                        trimmed.starts_with("struct ") || trimmed.starts_with("fn ") ||
                        trimmed.starts_with("def ") || trimmed.starts_with("function ") ||
                        trimmed.starts_with("namespace ") || trimmed.starts_with("template ") ||
                        trimmed.starts_with("public:") || trimmed.starts_with("private:") ||
                        trimmed.starts_with("protected:") || trimmed.starts_with("enum ") ||
                        trimmed.starts_with("void ") || trimmed.starts_with("int ") ||
                        trimmed.starts_with("auto ") || trimmed.starts_with("bool ") ||
                        trimmed.starts_with("static ") || trimmed.starts_with("virtual ")) {
                        outline_json.push_back({{"line", lineno}, {"text", trimmed}});
                    }
                }
                result.output = outline_json.dump();
                if (outline_json.empty()) {
                    result.output = nlohmann::json{{"outline", "No structural elements found"},
                        {"hint", "Use offset/limit to read sections"}}.dump();
                }
                return result;
            }

            // Auto-expand small range reads
            if (offset > 0 && limit > 0) {
                // If the requested range is < 16KB of content, return full file
                std::vector<std::string> lines_vec;
                std::istringstream iss(content);
                std::string line;
                while (std::getline(iss, line)) lines_vec.push_back(line);

                int total_lines = static_cast<int>(lines_vec.size());
                int start_idx = std::max(0, offset - 1);
                int end_idx = std::min(total_lines, offset - 1 + limit);

                std::ostringstream range_out;
                for (int i = start_idx; i < end_idx; ++i) {
                    range_out << lines_vec[i] << "\n";
                }

                if (range_out.str().size() < 16384) {
                    // Auto-expand to full file
                    result.output = content;
                } else {
                    result.output = nlohmann::json{
                        {"lines", range_out.str()},
                        {"start_line", offset},
                        {"end_line", end_idx},
                        {"total_lines", total_lines}
                    }.dump();
                }
                return result;
            }

            // Large file → downgrade to outline
            if (content.size() > 50000) {
                result.output = "File is large (" + std::to_string(content.size()) + " bytes). Use offset/limit or outline=true to read sections.";
                result.truncated = true;
                return result;
            }

            // Normal read
            result.output = content;
            spdlog::debug("ReadFile: read {} bytes from {}", result.output.size(), path);
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("ReadFile error: ") + e.what();
        }
        return result;
    });
}
```

- [ ] **Step 3: Build**

```bash
cd /home/icepop/Merak && cmake --build build -j 2>&1 | tail -10
```
Expected: clean（需要添加 `#include <set>` 到 fs_tools.cpp 顶部）

- [ ] **Step 4: Commit**

```bash
git add libs/tools/src/fs_tools.cpp libs/tools/include/merak/fs_tools.hpp
git commit -m "feat(tools): enhance read_file with offset/limit/outline, binary check, image base64"
```

---

# Phase D — write_file 增强

### Task D1: write_file 安全守卫

**Files:**
- Modify: `libs/tools/src/fs_tools.cpp`（WriteFileTool::execute()）

- [ ] **Step 1: 重写 execute()**

将 WriteFileTool::execute() 替换为带安全守卫的版本：

```cpp
std::future<ToolResult> WriteFileTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args["path"].get<std::string>();
            std::string content = args["content"].get<std::string>();
            std::string mode = args.value("mode", "overwrite");

            // 10 MB size guard
            if (content.size() > 10 * 1024 * 1024) {
                result.is_error = true;
                result.output = "Content too large (" + std::to_string(content.size()) + " bytes, max 10MB)";
                return result;
            }

            fs::path p(path);
            auto abs = fs::absolute(p);

            // Refuse .git/ contents
            if (abs.string().find("/.git/") != std::string::npos ||
                abs.filename() == ".git") {
                result.is_error = true;
                result.output = "Refusing to write inside .git/: " + path;
                return result;
            }

            // Refuse directory paths
            if (fs::exists(abs) && fs::is_directory(abs)) {
                result.is_error = true;
                result.output = "Path is a directory: " + path;
                return result;
            }

            // Create mode: refuse if file exists
            if (mode == "create" && fs::exists(abs)) {
                result.is_error = true;
                result.output = "File already exists (mode=create): " + path;
                return result;
            }

            // Check external modification (mtime-based)
            bool existed = fs::exists(abs);
            std::string old_content;
            auto old_mtime = fs::last_write_time(abs);
            if (existed) {
                std::ifstream in(abs);
                std::ostringstream oss;
                oss << in.rdbuf();
                old_content = oss.str();
            }

            fs::create_directories(abs.parent_path());

            std::ofstream f(abs);
            if (!f.is_open()) {
                result.is_error = true;
                result.output = "Cannot write to file: " + path;
                return result;
            }
            f << content;
            f.close();

            bool created = !existed;
            result.output = nlohmann::json{
                {"success", true},
                {"bytes_written", content.size()},
                {"path", path},
                {"created", created}
            }.dump();
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = std::string("WriteFile error: ") + e.what();
        }
        return result;
    });
}
```

- [ ] **Step 2: Build & commit**

```bash
cd /home/icepop/Merak && cmake --build build -j 2>&1 | tail -5
```
Expected: clean

```bash
git add libs/tools/src/fs_tools.cpp
git commit -m "feat(tools): add safety guards to write_file (.git/ block, 10MB limit, directory check)"
```

---

# Phase E — str_replace 模糊匹配

### Task E1: 在 StrReplaceTool 中添加模糊匹配级联

**Files:**
- Modify: `libs/tools/src/fs_tools.cpp`（StrReplaceTool::execute() — 修改匹配逻辑）

- [ ] **Step 1: 添加模糊匹配辅助函数并修改 execute()**

在 fs_tools.cpp 中的 StrReplaceTool::execute() 前添加：

```cpp
// Fuzzy match helpers for StrReplaceTool
namespace {
static std::string normalize_quotes(std::string s) {
    for (auto& c : s) {
        if (c == '“' || c == '”') c = '"';
        if (c == '‘' || c == '’') c = '\'';
    }
    return s;
}

static std::string normalize_leading_whitespace(const std::string& s) {
    std::istringstream iss(s);
    std::string line;
    std::vector<std::string> lines;
    size_t min_indent = std::string::npos;
    while (std::getline(iss, line)) {
        size_t indent = line.find_first_not_of(" \t");
        if (indent == std::string::npos) indent = line.size();
        if (indent < min_indent && !line.empty()) min_indent = indent;
        lines.push_back(line);
    }
    if (min_indent == std::string::npos || min_indent == 0) return s;
    std::string result;
    for (auto& l : lines) {
        if (!l.empty()) result += l.substr(min_indent);
        result += '\n';
    }
    return result;
}

static std::vector<size_t> find_all(const std::string& haystack, const std::string& needle) {
    std::vector<size_t> positions;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        positions.push_back(pos);
        pos += needle.size();
    }
    return positions;
}
} // namespace
```

然后修改 execute() 中 old_str 查找失败后的逻辑，在返回 error 之前添加模糊匹配级联：

在 `StrReplaceTool::execute()` 中，将：
```cpp
            size_t pos = content.find(old_str);
            if (pos == std::string::npos) {
                result.is_error = true;
                result.output = "old_str not found in file. Ensure exact whitespace match.";
                return result;
            }
```

替换为：
```cpp
            size_t pos = content.find(old_str);
            std::string active_old = old_str;
            int cascade_level = 0;

            // Fuzzy matching cascade
            if (pos == std::string::npos) {
                // Level 1: quote normalization
                auto normalized_old = normalize_quotes(old_str);
                auto normalized_content = normalize_quotes(content);
                pos = normalized_content.find(normalized_old);
                if (pos != std::string::npos) {
                    active_old = normalized_old;
                    content = normalized_content;
                    cascade_level = 1;
                }
            }
            if (pos == std::string::npos) {
                // Level 2: leading whitespace normalization
                auto nlo = normalize_leading_whitespace(old_str);
                auto nlc = normalize_leading_whitespace(content);
                pos = nlc.find(nlo);
                if (pos != std::string::npos) {
                    active_old = nlo;
                    content = nlc;
                    cascade_level = 2;
                }
            }
            if (pos == std::string::npos) {
                result.is_error = true;
                result.output = "old_str not found in file. Ensure exact whitespace match. "
                    "(Tried exact, quote-normalized, whitespace-normalized)";
                return result;
            }
```

- [ ] **Step 2: Build & commit**

```bash
cd /home/icepop/Merak && cmake --build build -j 2>&1 | tail -5
```
Expected: clean（需要添加 #include <sstream> 到 fs_tools.cpp）

```bash
git add libs/tools/src/fs_tools.cpp
git commit -m "feat(tools): add fuzzy matching cascade to str_replace"
```

---

# Phase F — 集成与收尾

### Task F1: 在 main.cpp 中注册新工具 + OutputCap

**Files:**
- Modify: `cli/src/main.cpp`
- Modify: `libs/tools/include/merak/tool_registry.hpp`

- [ ] **Step 1: 在 ToolRegistry 中添加 OutputCap**

在 `tool_registry.hpp` 中添加：

```cpp
// 在 namespace merak { 内，class ToolRegistry { public: 之前
struct OutputCap {
    size_t per_tool   = 50000;
    size_t grep       = 10000;
    size_t glob       = 100000;
    size_t aggregate  = 200000;
    size_t soft       = 120000;
    size_t persist_threshold = 50000;
};
```

在 `ToolRegistry` 类的 public 区域添加：
```cpp
    void set_output_caps(const OutputCap& caps) { caps_ = caps; }
    const OutputCap& caps() const { return caps_; }
```

在 `ToolRegistry` 类的 private 区域添加：
```cpp
    OutputCap caps_;
```

(spec §10)

- [ ] **Step 2: 更新 main.cpp 中的工具注册**

在 `cli/src/main.cpp:67`，将当前的工具注册：
```cpp
    auto tools=std::make_shared<ToolRegistry>();
    tools->register_tool(std::make_unique<tools::ReadFileTool>());
    tools->register_tool(std::make_unique<tools::WriteFileTool>());
    tools->register_tool(std::make_unique<tools::EditFileTool>());
    tools->register_tool(std::make_unique<tools::GlobTool>());
    tools->register_tool(std::make_unique<tools::GrepTool>());
    tools->register_tool(std::make_unique<tools::BashTool>());
    tools->set_permission_mode(cfg.agent.permission_mode);
```

替换为：
```cpp
    auto tools=std::make_shared<ToolRegistry>();
    tools->register_tool(std::make_unique<tools::ReadFileTool>());
    tools->register_tool(std::make_unique<tools::WriteFileTool>());
    tools->register_tool(std::make_unique<tools::StrReplaceTool>());
    tools->register_tool(std::make_unique<tools::MultiEditTool>());
    tools->register_tool(std::make_unique<tools::DeleteFileTool>());
    tools->register_tool(std::make_unique<tools::ListDirTool>());
    tools->register_tool(std::make_unique<tools::GlobTool>());
    tools->register_tool(std::make_unique<tools::GrepTool>());
    tools->register_tool(std::make_unique<tools::BashTool>());
    tools->set_permission_mode(cfg.agent.permission_mode);
```

- [ ] **Step 3: Build & commit**

```bash
cd /home/icepop/Merak && cmake --build build -j 2>&1 | tail -10
```
Expected: clean

```bash
git add cli/src/main.cpp libs/tools/include/merak/tool_registry.hpp
git commit -m "feat(tools): register new tools in main.cpp, add OutputCap to ToolRegistry"
```

---

### Task F2: End-to-end build & smoke

- [ ] **Step 1: Full build**

```bash
cd /home/icepop/Merak && cmake --build build -j 2>&1 | tail -10
```
Expected: clean, no warnings.

- [ ] **Step 2: Lint check**

```bash
ls libs/tools/include/merak/fs_tools.hpp && ls libs/tools/include/merak/search_tools.hpp && ls libs/tools/include/merak/shell_tool.hpp && ls libs/tools/include/merak/edit_journal.hpp && ls libs/tools/src/fs_tools.cpp && ls libs/tools/src/search_tools.cpp && ls libs/tools/src/shell_tool.cpp && ls libs/tools/src/edit_journal.cpp
```
Expected: all 8 files exist

- [ ] **Step 3: Help check**

```bash
./build/cli/merak --help
```
Expected: prints usage.

- [ ] **Step 4: TUI launch (if server available)**

```bash
./build/cli/merak tui
```
Expected: starts, shows welcome. Send a message to verify streaming still works.

- [ ] **Step 5: Commit**

```bash
git commit -m "chore(tools): end-of-phase smoke check — build clean, merak --help ok" --allow-empty
```

- [ ] **Step 6: Tag**

```bash
git tag tool-system-upgrade/v1
git log --oneline -15
```

---

## 验证清单

- [ ] `libs/tools/src/builtin_tools.cpp` 已删除
- [ ] `builtin_tools.hpp` 是聚合头文件（3 个 include）
- [ ] `fs_tools.hpp` 声明 6 个工具类：ReadFileTool, WriteFileTool, StrReplaceTool, MultiEditTool, DeleteFileTool, ListDirTool
- [ ] `search_tools.hpp` 声明 GlobTool, GrepTool
- [ ] `shell_tool.hpp` 声明 BashTool
- [ ] `edit_journal.hpp` 声明 EditJournal
- [ ] `main.cpp` 注册了 9 个工具（+3 个新增）
- [ ] `ToolResult` 有 4 个新字段（truncated, exit_code, duration_ms, cached）
- [ ] `ToolRegistry` 有 OutputCap
- [ ] `cmake --build build -j` clean
- [ ] `./build/cli/merak --help` 正常
