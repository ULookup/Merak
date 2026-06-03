# Merak Agent

> 一个用于学习现代 Agent Runtime 内部机制的 C++23 项目。当前形态是本地常驻 Agent 服务端，加上独立 TUI 客户端。

## 项目定位

Merak 不以生产部署为目标，而是通过可阅读、可拆解的实现理解现代 Coding Agent 的核心机制：

- **Agent Loop**：LLM 推理、工具调用、结果观察和多轮状态机。
- **Runtime**：Session、Run、事件流、审批、取消和恢复。
- **上下文管理**：Token 预算、前缀缓存意识、历史压缩和摘要。
- **工具系统**：内置工具、权限检查、MCP 动态工具和取消传播。
- **持久化**：SQLite 当前状态索引和 JSONL 追加式事件日志。
- **终端客户端**：通过 HTTP + SSE 连接 runtime 的 TUI，不持有 Agent 核心状态。

## 当前架构

Merak 已从单进程 CLI 拆分为两个运行角色：

```text
merak tui
   │
   │ HTTP + SSE
   ▼
merak serve                     仅监听 127.0.0.1
   │
   ├── merak-http               REST 路由、SSE backlog/live stream
   ├── merak-runtime            Session、Run、EventBus、审批、取消
   ├── merak-storage            SQLite 索引、JSONL journal、启动恢复
   ├── merak-loop               Agent 状态机、工具循环、上下文压缩
   ├── merak-context            上下文组装、Token 预算、缓存前缀分析
   ├── merak-tools              内置工具、权限、MCP 包装
   ├── merak-mcp                MCP stdio 客户端、远端工具发现
   ├── merak-llm                OpenAI / Anthropic 流式 Provider
   ├── merak-memory             工作记忆、pgvector 长期记忆接口
   ├── merak-config             分层配置加载
   └── merak-core               共享类型、执行端口、错误定义
```

TUI 只负责输入和渲染。LLM Provider、工具、MCP、Memory 和 AgentLoop 全部由 `merak serve` 初始化。

### 依赖关系

```text
merak-core
├── merak-config
├── merak-mcp
├── merak-llm
├── merak-memory
├── merak-storage
├── merak-tools        ← core, mcp
├── merak-context      ← core, memory, llm
├── merak-loop         ← core, config, llm, memory, tools, context
├── merak-runtime      ← core, storage, loop
├── merak-http         ← core, runtime
├── merak-worldbuilding ← SQLite, nlohmann_json
└── merak-cli          ← HTTP server composition root + TUI HTTP/SSE client
```

## Runtime 模型

### Session

Session 是长期存在的对话线程。关闭 TUI 不会关闭 Session。

```text
active → archived
```

### Run

每次用户消息会创建一个 Run。同一 Session 同时只允许一个未完成 Run。

```text
queued
  → running
  → waiting_approval
  → running
  → completed

running          → failed
running          → cancelled
running          → interrupted
waiting_approval → cancelled
```

服务端重启时：

- `running` Run 会标记为 `interrupted`。
- `waiting_approval` Run 会保留；审批后可从 journal 重建消息并继续。
- 正在执行中的网络请求或 Shell 进程不会跨进程恢复。

### 事件流

Runtime 先追加 JSONL journal，再更新 SQLite 索引，最后广播 SSE。TUI 断线重连时携带最后一个 `seq`，服务端先补发缺失事件，再继续发送实时事件。

公开事件包括：

```text
session_created
run_started
state_changed
text_delta
usage_updated
tool_started
tool_completed
approval_requested
approval_resolved
delegation_started
sub_run_started
sub_run_state_changed
sub_run_text_delta
sub_run_tool_started
sub_run_tool_completed
sub_run_completed
delegation_completed
run_completed
run_failed
run_cancelled
run_interrupted
```

内部恢复记录包括：

```text
message_appended
compaction_applied
```

## HTTP API

`merak serve` 默认监听 `127.0.0.1:3888`。第一版没有鉴权，因此不提供局域网监听参数。

```text
GET  /v1/runtime
POST /v1/sessions
GET  /v1/sessions
GET  /v1/sessions/{id}
GET  /v1/sessions/{id}/events?after={seq}
GET  /v1/sessions/{id}/events/stream?after={seq}
POST /v1/sessions/{id}/runs
POST /v1/sessions/{id}/delegations
POST /v1/approvals/{id}
POST /v1/runs/{id}/cancel
```

## Worldbuilding Novel Agent

`merak-worldbuilding` 是世界观/长篇小说创作领域库。详情见 [docs/worldbuilding-novel-agent.md](docs/worldbuilding-novel-agent.md)。

核心能力：
- **World 隔离**：每个世界独立目录 + SQLite 索引
- **Agent 分层**：God（全知叙事者）→ Manager（地图/历史/魔法/势力）→ Character（个体/群体）
- **叙事骨架**：Arc → Chapter → Section → Scene 层级
- **伏笔系统**：Open/Paid/Abandoned 生命周期，最后一幕提醒
- **秘密系统**：三态信息不对称（Public/Secret/Unknown），泄密检测
- **声音指纹**：句式、修饰词、签名词分析，不自动改写角色
- **角色记忆**：角色卡版本历史、日记、关系、记忆摘要

`merak serve` 启动时初始化 WorldbuildingService，数据落在 `~/.merak/worlds/{world_id}`。

## Agent Loop

单个 Run 内的状态机：

```text
IDLE
  → CONTEXT_READY
  → THINKING
  → [ACTING → OBSERVING → CONTEXT_READY] × N
  → RESPONDING
  → COMPLETE
```

安全机制：

- 最多 25 轮工具调用，可配置。
- 工具连续失败 3 次后触发熔断。
- Token 压力过高时压缩历史，并将摘要写入 journal。
- OpenAI 和 Anthropic 请求支持通过 libcurl progress callback 取消。
- Shell 使用独立进程组，取消 Run 时终止整个子进程组。
- MCP 等无法立即中止的调用会在返回后丢弃已取消 Run 的结果。

## 工具系统

当前注册 6 个内置工具：

| 工具 | 权限 | 用途 |
|------|------|------|
| `read_file` | `safe` | 读取文件 |
| `write_file` | `ask` | 写入文件 |
| `edit_file` | `ask` | 精确替换文件内容 |
| `glob` | `safe` | 匹配文件 |
| `grep` | `safe` | 搜索代码 |
| `execute_bash` | `ask` | 执行 Shell 命令 |

MCP Server 可通过配置启动，并将远端工具动态注册进同一 `ToolRegistry`。

## 持久化

默认存储目录：

```text
~/.merak/runtime.sqlite3
~/.merak/sessions/{session_id}.jsonl
```

- SQLite 保存 Session、Run 和 Approval 的当前状态。
- JSONL 保存不可变事件历史，用于审计、SSE 补发和恢复。
- 服务端启动时会根据 journal 修复 SQLite 中落后的 `last_seq`。

## 构建与运行

### 依赖

- GCC >= 13 或 Clang >= 17
- CMake >= 3.22
- Conan >= 2.0
- PostgreSQL >= 14 和 pgvector，可选

主要第三方库：

| 类别 | 技术 |
|------|------|
| 语言 | C++23 |
| 构建 | CMake + Conan 2 |
| HTTP Server | cpp-httplib |
| HTTP Client | libcurl |
| Runtime 状态 | SQLite |
| 长期记忆 | PostgreSQL + pgvector |
| JSON | nlohmann/json |
| 日志 | spdlog |
| TUI | FTXUI + inline terminal renderer |

### 安装依赖并构建

```bash
conan install . --build=missing -s build_type=Debug
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

### 初始化

```bash
./build/cli/merak --init
```

编辑 `~/.merak/settings.local.json`，填入 API Key。

### 启动

在一个终端启动服务端：

```bash
./build/cli/merak serve
```

在另一个终端启动 TUI：

```bash
./build/cli/merak tui
```

恢复已有 Session：

```bash
./build/cli/merak tui --session <session_id>
```

### TUI 命令

```text
/session list
/session new
/session use <id>
/tools
/agents
/team fanout <agent1,agent2> <task>
/team sequential <agent1,agent2> <task>
/team pipeline <agent1,agent2> <task>
/context
/transcript
/tool-calls
/clear
/exit
```

执行期间按 `Ctrl+C` 会先取消当前 Run；空闲时按 `Ctrl+C` 退出 TUI。

### 测试

```bash
cd build
ctest --output-on-failure
```

测试源码覆盖 storage、runtime、HTTP 契约和 TUI runtime client。

## 项目结构

```text
Merak/
├── libs/
│   ├── core/          # 共享类型、执行端口
│   ├── config/        # 配置加载
│   ├── llm/           # OpenAI / Anthropic Provider
│   ├── memory/        # 工作记忆、长期记忆接口
│   ├── mcp/           # MCP stdio 客户端
│   ├── tools/         # 内置工具、ToolRegistry
│   ├── context/       # 上下文预算、压缩
│   ├── loop/          # AgentLoop、SubAgentRunner
│   ├── storage/       # SQLite + JSONL
│   ├── runtime/       # Session、Run、EventBus、Approval
│   ├── http/          # REST + SSE
│   └── worldbuilding/ # World、Agent、Narrative、Foreshadowing、Secret、Voice
├── cli/               # serve / tui 入口和终端客户端
└── tests/             # CTest 注册
```

## 当前边界

已有实现但仍需后续完善：

- 长期记忆 pgvector 接口已存在，但 CLI composition root 尚未接入 EmbeddingProvider。
- `SubAgentRunner` 提供委派、fan-out 和串行执行库能力，但尚未暴露为 TUI 工作流。
- 测试源码已补齐，但当前提交没有执行 Conan 安装、编译或测试验证。

暂未包含：

- Web UI
- 鉴权和多用户隔离
- 远程 Edge 工具执行
- Shell 沙箱
- Hook / Skill / Plugin 系统
- 后台 Durable Job
- 云端部署

## 参考资料

- 本地 `../astra` 项目：Runtime、Session Journal、审批与恢复设计参考
- Claude Code / Codex CLI 架构分析
- [Model Context Protocol](https://modelcontextprotocol.io/)

## 许可证

MIT
