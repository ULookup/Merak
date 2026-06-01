# Merak Agent

> 一个用于深入学习 Agent 内部机制的 C++20 教学项目 — 从零构建类似 Claude Code/Codex CLI 的智能助手。

## 目标

Merak 的目标并非生产可用，而是**通过亲手实现来理解现代 AI Agent 的六大核心系统**：

- **对话循环 (Agent Loop)** — 状态机驱动的多轮对话，LLM 思考 → 工具调用 → 结果观察 → 继续思考
- **上下文管理 (Context)** — Token 预算分配、前缀缓存优化、历史压缩与摘要
- **记忆系统 (Memory)** — 短期工作记忆 + 基于 pgvector 的长期语义记忆
- **工具调用 (Tools)** — 统一工具注册表，内置工具与 MCP 远端工具透明调度
- **多 Agent 协作 (Multi-Agent)** — Fan-out 并行、Sequential 串行、Adversarial 对抗三种分发模式
- **MCP 集成** — 通过 Model Context Protocol 接入外部工具生态

## 技术栈

| 类别 | 技术 |
|------|------|
| 语言 | C++20（协程、concepts、ranges） |
| 构建 | CMake + Conan 2 |
| 异步 | Boost.Asio |
| 数据库 | PostgreSQL 14+ + pgvector |
| JSON | nlohmann/json |
| HTTP | libcurl |
| 日志 | spdlog |
| 测试 | Google Test |

## 架构

9 个模块，自底向上分层构建，每个模块编译为独立静态库：

```
merak-cli        ← 终端交互，流式渲染
merak-loop       ← 对话循环，状态机，多 Agent 调度
merak-context    ← 上下文组装，Token 预算，压缩
merak-tools      ← 工具注册表，统一调度，权限检查
merak-llm        ← LLM Provider 抽象，流式调用，缓存策略
merak-mcp        ← MCP 客户端，远端工具发现
merak-memory     ← 短期/长期记忆，向量检索
merak-config     ← 模型配置，密钥，全局参数
merak-core       ← 共享类型，错误定义，JSON 序列化
```

### 依赖关系

```
merak-core        ← 无依赖
merak-config      ← core
merak-llm         ← core, config
merak-memory      ← core, config
merak-mcp         ← core, config
merak-tools       ← core, mcp
merak-context     ← core, memory
merak-loop        ← core, context, tools, llm, memory
merak-cli         ← loop, config
```

## 构建与运行

### 前置依赖

- GCC >= 13 或 Clang >= 17
- CMake >= 3.25
- Conan >= 2.0
- PostgreSQL >= 14（长期记忆，可选）
- pgvector 扩展（长期记忆，可选）

### 安装依赖并构建

```bash
# 安装 Conan 依赖
conan install . --build=missing -s build_type=Debug

# 配置 CMake
cmake -B build -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build build -j$(nproc)

# 运行 CLI
./build/cli/merak-cli --config config.json
```

### 运行测试

```bash
cd build && ctest --output-on-failure
```

## 项目结构

```
Merak/
├── CMakeLists.txt
├── conanfile.txt
├── libs/
│   ├── core/        # merak-core: 共享类型、错误
│   ├── config/      # merak-config: 配置加载
│   ├── llm/         # merak-llm: LLM Provider
│   ├── memory/      # merak-memory: 记忆系统
│   ├── mcp/         # merak-mcp: MCP 客户端
│   ├── tools/       # merak-tools: 工具注册
│   ├── context/     # merak-context: 上下文管理
│   └── loop/        # merak-loop: 对话循环
├── cli/             # merak-cli: 终端前端
├── tests/           # 集成测试
└── docs/            # 设计与计划文档
```

## 关键设计原则

- **核心库与 CLI 分离** — merak-loop 及以下都是独立库，可嵌入其他程序
- **异步优先** — 所有 IO 操作基于 C++20 协程，不阻塞线程
- **统一抽象** — 对 Loop 而言，工具来源（内置/MCP）和记忆存储后端完全透明
- **追加不修改** — 消息历史只追加，保证 LLM Prompt 缓存前缀命中
- **自底向上构建** — 从零依赖的 core 开始，逐层叠加能力

## 对话循环状态机

```
IDLE → CONTEXT_READY → THINKING → [ACTING → OBSERVING → CONTEXT_READY]×N → RESPONDING → COMPLETE
```

- **IDLE** — 等待用户输入
- **CONTEXT_READY** — 上下文组装完成（SystemPrompt + History + Tools + Memory）
- **THINKING** — 向 LLM 发送请求，等待流式响应
- **ACTING** — 执行 LLM 请求的工具调用
- **OBSERVING** — 工具结果注入上下文，准备下一轮
- **RESPONDING** — 流式输出文本给用户
- **COMPLETE** — 回复完成

安全阀：最多 25 轮工具调用，Token 预算超限触发上下文压缩。

## 内存 / 记忆系统

| 层级 | 范围 | 存储 | 内容 |
|------|------|------|------|
| 短期记忆 | 当前 Session | 内存 | 对话历史、压缩摘要、任务上下文 |
| 长期记忆 | 跨 Session | PostgreSQL + pgvector | 对话摘要、用户偏好、项目信息 |

长期记忆支持语义检索（top-K 向量搜索），置信度随时间衰减。

## 工具系统

内置 9 个默认工具，3 级权限模型：

- `auto` — 只读操作自动通过（如 read_file、grep、glob）
- `ask` — 写入/执行操作需用户确认（如 write_file、execute_bash）
- `deny` — 特定工具完全禁止

通过 MCP 协议可动态注册外部工具，对对话循环完全透明。

## 范围边界

以下能力是生产级 Agent 的重要组成部分，但本学习项目暂不包含：

- Hook 系统
- ML 分类器权限判定
- Shell 沙箱隔离
- 多租户支持
- 分布式部署
- E2E 测试框架
- Plugin 插件系统
- 性能压测

## 参考资料

- [astra-engine](https://github.com/username/astra-engine) — 架构参考
- Claude Code 源码分析
- Codex CLI 架构
- [Model Context Protocol (MCP)](https://modelcontextprotocol.io/)

## 许可证

MIT
