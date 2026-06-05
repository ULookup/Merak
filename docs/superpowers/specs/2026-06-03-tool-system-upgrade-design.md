# Merak 工具系统升级 — 设计

**Date:** 2026-06-03
**Status:** Draft
**Scope:** `libs/tools/` 全部文件；`libs/core/include/merak/message.hpp`（ToolResult 加字段）
**Reference:** Astra `rust/crates/astra-cli/src/edge_tools/` — `shell.rs`, `fs.rs`, `edge_tools.rs`

---

## 1. Why

当前 6 个内置工具 (`read_file`, `write_file`, `edit_file`, `glob`, `grep`, `execute_bash`) 在安全边界、大文件处理、错误恢复方面有多处缺陷：

- `read_file` 无二进制检测、无偏移/行数限制、无 outline 降级
- `write_file` 无 .git/ 保护、无大小上限
- `edit_file` 纯 exact match，无模糊匹配，无批量原子编辑
- `glob` 自写匹配器 `**` 递归有 bug
- `grep` 只搜 `.cpp/.hpp/.h/.c` 后缀
- `execute_bash` 危险命令黑名单太弱、60s 去重对非幂等操作有风险
- 缺失 `list_dir`、`delete_file` 等基本文件操作

参照 Astra 的 `fs.rs` + `shell.rs` 实现，升级所有内置工具并新增缺失工具。

---

## 2. 文件布局

```
libs/tools/
├── include/merak/
│   ├── tool_base.hpp           # 不变
│   ├── tool_registry.hpp       # 加 output cap 配置
│   ├── builtin_tools.hpp       # 聚合头文件（向后兼容）
│   ├── fs_tools.hpp            # read_file / write_file / str_replace (StrReplaceTool) / multi_edit / delete_file / list_dir
│   ├── shell_tool.hpp          # execute_bash（升级安全检查）
│   ├── search_tools.hpp        # glob / grep
│   ├── mcp_tool_wrapper.hpp    # 不变
│   └── edit_journal.hpp        # 文件编辑日志（undo 支持）
├── src/
│   ├── fs_tools.cpp
│   ├── shell_tool.cpp
│   ├── search_tools.cpp
│   ├── tool_registry.cpp       # 小幅修改
│   └── edit_journal.cpp
└── tests/
    ├── test_fs_tools.cpp
    ├── test_shell_tool.cpp
    └── test_search_tools.cpp
```

---

## 3. read_file 增强

**新增参数：** `offset` (行号), `limit` (行数), `outline` (bool)

**安全 & 行为：**
- 二进制检测：前 1024 字节含 NUL → 报错 "binary file"
- 图片 (.png/.jpg/.gif/.bmp/.webp)：≤1.5MB → base64 data URI；>1.5MB → 报错
- 设备文件拒绝
- 大文件 (>50KB)：返回 outline + hint "use offset/limit"
- outline v1：启发式提取缩进 + 前导关键字（`class `, `struct `, `fn `, `def `, `namespace `, `#`）
- 小范围读取 (<16KB) 自动展开为全文件
- 内容未变时去重：返回 `{"unchanged": true}`
- 输出上限 50KB（ToolRegistry.output_cap 可配置）

**权限：** `PermissionLevel::safe`

---

## 4. write_file 增强

**新增参数：** `mode` (create/overwrite)

**安全守卫：**
- 拒绝 >10MB 的 content
- 拒绝 `.git/` 子目录
- 拒绝目录路径
- 父目录不存在时自动 create_directories
- 外部修改检测：mtime 变化时返回警告 + diff

**返回：** `{"success": true, "bytes_written": N, "path": "...", "created": bool}`

**权限：** `PermissionLevel::ask`

---

## 5. str_replace（原 edit_file 重命名） + multi_edit

### str_replace

**参数：** `path`, `old_str`, `new_str`, `replace_all`, `dry_run`

**模糊匹配级联（exact match 失败时依次尝试）：**
1. 精确匹配
2. 引号规范化（curly quotes → straight quotes）
3. 前导空白规范化
4. 整体空白规范化

**安全：**
- `dry_run` 只返回 diff 不写文件
- 唯一性检查（`replace_all=false` 时，old_str 必须匹配恰好一处）
- 写入后文件大小 >10MB → 回滚 + 报错

### multi_edit（新增）

**参数：** `path`, `edits: [{old_str, new_str}, ...]`
**行为：** 所有编辑成功才写入，否则全部取消（原子）
**overlap 检测：** 模糊匹配区间有重叠 → 拒绝

**权限：** `PermissionLevel::ask`

---

## 6. delete_file + list_dir（新增）

### delete_file

**参数：** `path`, `explanation`（为什么删）

**安全：**
- 拒绝 `.git/` 子目录内容
- 拒绝目录路径
- 删除前写 edit_journal（可 undo）
- 文件不存在 → `{"deleted": false, "reason": "not found"}`

### list_dir

**参数：** `path` (默认 `.`), `depth` (默认 1, 最大 10), `show_hidden` (默认 false)

**行为：**
- 自动跳过 `node_modules/`, `target/`, `__pycache__`, `.git/`, `build/`
- 符号链接跟随 + 循环检测（canonical path set）
- 上限 500 项，超出截断

**权限：** `PermissionLevel::safe`

---

## 7. glob + grep 修复

### glob

**改为系统调用：** 替换自写 `match_glob()`，用 POSIX `glob(3)` 或手动转译到 `std::filesystem`
- 上限 100 个匹配
- 去重 + 按文件名排序

### grep

**去除文件扩展名限制** —— 搜索所有文本文件（二进制检测后跳过）

**新增参数：** `include` (glob pattern), `exclude` (glob pattern)

**默认 include（未指定时）：**
```
*.{cpp,hpp,c,h,cc,cxx,py,js,ts,go,rs,java,kt,swift,md,txt,json,yaml,yml,toml,cfg,ini,sh,bash,zsh,cmake,Makefile,gradle,html,css,xml,svg}
```

**输出上限：** 10KB

---

## 8. execute_bash 安全检查升级

### 5 层安全检查

| 层 | 描述 |
|----|------|
| 1 — 危险模式 | `rm -rf /`, `mkfs.*`, `dd if=* of=/dev/*`, `chmod 777 /`, `> /dev/sda`, `curl | sudo`, `base64 -d | sh` |
| 2 — 不可逆 git | 拒绝 `git push --force` 到 main/master、`reset --hard`、`clean -fdx` |
| 3 — 变量绕过 | 解析 `alias`、`$()`、反引号中的危险命令 |
| 4 — SQL 破坏 | 拒绝 DROP TABLE/DATABASE、TRUNCATE、无 WHERE 的 DELETE |
| 5 — sandbox path | 所有写入目标路径必须在项目目录内 |

### 去重缓存修正

- 移除全局 60s 去重
- 仅对 10 个只读命令去重：`ls`, `find`, `cat`, `head`, `tail`, `wc`, `du`, `stat`, `file`, `pwd`

### exit code 语义

- grep/rg exit 1 = "no matches"
- diff exit 1 = differences found
- find exit 1 = no matches
- 以上 exit code 不产生 error result

### 返回增强

```json
{"output": "...", "exit_code": 0, "duration_ms": 234, "cached": false}
```

**权限：** `PermissionLevel::ask`

---

## 9. edit_journal（撤销支持）

```cpp
class EditJournal {
public:
    void record_edit(const std::filesystem::path& path,
                     const std::string& before,
                     const std::string& after);
    
    struct Entry {
        std::filesystem::path path;
        std::string before;    // empty if file was new (created)
        std::string after;     // empty if file was deleted
        std::chrono::steady_clock::time_point time;
    };
    
    std::vector<Entry> recent_edits() const;
    bool rollback(size_t count = 1);   // undo last N edits
    void clear();
    
private:
    std::vector<Entry> journal_;
    static constexpr size_t kMaxEntries = 100;
};
```

生命周期：绑定到 `ToolRegistry`，每次工具执行时 journal 写操作。

---

## 10. output cap 系统

`ToolRegistry` 新增：

```cpp
struct OutputCap {
    size_t per_tool   = 50000;   // 单工具默认上限
    size_t grep       = 10000;
    size_t glob       = 100000;
    size_t aggregate  = 200000;  // 单轮累计上限
    size_t soft       = 120000;  // 超此触发降级（全文件→outline）
    size_t persist_threshold = 50000;
};
```

工具从 `ToolRegistry` 读取 caps，自行截断 + 追加 `[truncated N bytes]` 标记。

---

## 11. ToolResult 扩展

`libs/core/include/merak/message.hpp` 中 `ToolResult` 新增字段：

```cpp
struct ToolResult {
    std::string call_id;
    std::string output;
    bool is_error = false;
    
    // 新增
    bool truncated = false;
    int exit_code = 0;
    long duration_ms = 0;
    bool cached = false;
};
```

向后兼容：新字段有默认值，旧代码不受影响。

---

## 12. 权限表

| 工具 | 权限 | 并发安全 |
|------|------|---------|
| read_file | safe | yes |
| write_file | ask | no |
| str_replace | ask | no |
| multi_edit | ask | no |
| delete_file | ask | no |
| list_dir | safe | yes |
| glob | safe | yes |
| grep | safe | yes |
| execute_bash | ask | 仅白名单命令 |

---

## 13. 向后兼容

- `builtin_tools.hpp` 保留为聚合头文件，现有 `#include <merak/builtin_tools.hpp>` 继续工作
- `ReadFileTool` / `WriteFileTool` 等类名不变，只是参数和行为增强
- `edit_file` → `str_replace` 重命名：保留一个 `EditFileTool` 别名类（`using EditFileTool = StrReplaceTool`）
- `execute_bash` tool name 保持不变
- ToolSpec 字段不变

---

## 14. Non-goals

- 不做 git 操作工具（git_status/diff/log/blame）—— Merak 定位是 agent 框架，不是代码管理工具
- 不做 web_search —— 可后续通过 MCP 接入
- 不做 notebook_edit —— 等有实际需求再考虑
- 不做 tree-sitter 集成 —— outline 用启发式，够用
- 不引入新依赖（除了标准库 + 现有的 nlohmann/json）

---

## 15. Open questions

1. edit_journal 应持久化到磁盘还是仅内存？首版内存即可，crash 丢失可接受。
2. sandbox path check 的"项目目录"边界 — 用当前工作目录还是 git root？首版用 `cwd`，后续可配置。
3. 工具测试策略 — 参考先前的"不新增测试"共识，本次也不新增测试。但 shell 安全检查的每个层级需要手动验证。
