<p align="center">
  <img src="webui/src/assets/merak-logo-lockup.svg" alt="Merak - type your world" width="420" />
</p>

# Merak

> type your world

Merak 是面向长篇小说与世界观创作的 AI Agent 运行时。它把聊天、角色、伏笔、叙事结构和本地文件产出放进同一个工作流里，让 AI 不只是回答问题，而是陪你稳定地搭建一个世界。

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## 为什么是 Merak

通用 AI 聊天工具很适合灵感闪现，却很难长期记住一个复杂故事。Merak 的目标是把“写作上下文”变成一等公民：

| 创作问题 | 常见表现 | Merak 的处理方式 |
|----------|----------|------------------|
| 角色遗忘 | 写到后面，角色动机、经历和关系被稀释 | 角色卡、日记、关系网和记忆摘要随故事推进持续演化 |
| 语言同质化 | 将军、公主、旁白都像同一个人在说话 | 角色声音指纹锚定句式、修饰词和签名表达 |
| 世界观漂移 | 前文规则被后续生成破坏 | World 系统集中维护地图、历史、规则和势力关系 |
| 伏笔丢失 | 埋下的线索没有回收 | Open / Paid / Abandoned 生命周期追踪 |
| 情节跃进 | AI 为了收束，跳过必要铺垫 | Arc → Chapter → Section → Scene 叙事骨架约束推进 |
| 角色全知 | 角色知道了自己不该知道的信息 | Public / Secret / Unknown 信息边界与泄密检测 |
| 上下文稀释 | 长篇后半段理解越来越模糊 | Token 预算、前缀缓存和历史压缩保留关键叙事信息 |

## 核心体验

- **创作者工作台**：现代可视化界面，左侧管理 World / Sessions / Model / Tools / Context，中间承载聊天与工具时间线，右侧检查 Story / Files / Agents / Run。
- **本地文件产出**：Agent 在用户指定目录生成章节、设定或草稿文件；Web UI 的 Files 面板提供入口。
- **双击即编辑**：在 Files 面板双击生成文件，打开内置文本编辑器进行本地草稿编辑。
- **分层 Agent 协作**：God 负责全局叙事，Manager 负责领域设定，Character 在自身知识边界内行动。
- **数据主权**：数据落在本机 PostgreSQL + JSONL，并由 `~/.merak/` 管理。你的故事永远属于你。

## 和 SillyTavern 的区别

SillyTavern 是优秀的 AI 角色扮演前端。Merak 可以和角色聊天，但聊天不是终点，而是创作流程的一部分。

| 维度 | SillyTavern | Merak |
|------|-------------|-------|
| 核心体验 | 和 AI 角色聊天互动 | 在世界观框架下推进小说创作 |
| 角色记忆 | 依赖上下文窗口 | 角色卡版本、日记、关系网、记忆摘要 |
| 世界观一致性 | Lorebook 手动维护 | World 系统自动注入锚定上下文 |
| 角色协作 | 群聊式同台对话 | God / Manager / Character 分层协作 |
| 信息不对称 | 通常无结构化约束 | Public / Secret / Unknown 知识边界 |
| 叙事管理 | 通常无 | Arc / Chapter / Section / Scene 与伏笔生命周期 |
| 输出产物 | 聊天记录 | 聊天记录、世界观文档、角色档案、章节文件 |

## 架构一览

Merak 采用服务端-客户端分离架构。服务端负责 Agent Loop、工具、上下文、持久化与 HTTP/SSE；客户端负责输入、渲染和创作者工作台体验。

```text
┌─────────────────────────────────────────┐
│                Clients                  │
│  ┌──────────┐          ┌──────────────┐ │
│  │   TUI    │          │    Web UI    │ │
│  │  FTXUI   │          │ React + Vite │ │
│  └────┬─────┘          └──────┬───────┘ │
│       │        HTTP + SSE      │         │
└───────┼────────────────────────┼─────────┘
        │                        │
        ▼                        ▼
┌──────────────────────────────────────────┐
│                merak serve               │
│  HTTP Layer  · REST routes · SSE events  │
│  Runtime     · Session · Run · Approval  │
│  AgentLoop   · State machine · Tools     │
│  Context     · Token budget · Compression│
│  Tools       · Built-in tools · MCP      │
│  Worldbuilding · World · Agent · Story   │
│  Storage     · PostgreSQL · JSONL        │
└──────────────────────────────────────────┘
```

## Worldbuilding 引擎

### 分层 Agent

| Agent | 职责 | 例子 |
|-------|------|------|
| God | 全知叙事者，把控整体剧情、节奏和基调 | “第三章高潮偏弱，需要再铺垫一场冲突。” |
| Manager | 专业领域顾问，维护地图、历史、魔法体系、势力关系 | “这个世界没有传送魔法，跨大陆至少需要两周。” |
| Character | 角色模拟，在自身知识边界内行动 | 作为角色回应事件、写日记、更新关系态度 |

### 叙事骨架

```text
Arc
└── Chapter
    └── Section
        └── Scene
```

每个节点都可以附加独立设定和上下文，让 AI 永远知道当前正在推进故事的哪一层。

### 伏笔与信息边界

- 伏笔状态：`Open`、`Paid`、`Abandoned`
- 角色知识：`Public`、`Secret`、`Unknown`
- 角色声音：句式偏好、修饰词风格、签名词
- 角色记忆：角色卡版本、日记、关系网、记忆摘要

## 快速开始

### 环境要求

| 依赖 | 版本 |
|------|------|
| GCC 或 Clang | GCC >= 13 / Clang >= 17 |
| CMake | >= 3.22 |
| Conan | >= 2.0 |
| PostgreSQL | >= 14 |
| Node.js | 建议 LTS |

### 构建服务端

```bash
conan install . --build=missing -s build_type=Debug

cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build -j
./build/cli/merak --init
```

编辑 `~/.merak/settings.local.json`，填入 LLM API Key。

### 启动

```bash
# terminal 1
./build/cli/merak serve

# terminal 2: TUI
./build/cli/merak tui

# or Web UI
cd webui
npm install
npm run dev
```

Web UI 默认连接 `http://localhost:3888`，本地开发地址通常是 `http://localhost:5173`。

恢复已有 Session：

```bash
./build/cli/merak tui --session <session_id>
```

## Web UI

Web UI 是 Merak 的亮色创作者工作台：

- 左侧：World、Sessions、Model、Tools、Context
- 中间：对话时间线、工具运行状态、审批与 composer
- 右侧：Story 上下文、生成文件入口、Agent 列表、Run/Context 检查器
- Files：展示输出目录和已生成文件，双击文件即可打开文本编辑器
- Motion：streaming shimmer、live pulse、panel reveal、message/tool enter，并支持 `prefers-reduced-motion`

```bash
cd webui
cp .env.example .env
npm install
npm run dev
```

## 项目结构

```text
Merak/
├── libs/
│   ├── core/             共享类型 · 执行端口
│   ├── config/           分层配置加载
│   ├── llm/              OpenAI / Anthropic Provider
│   ├── memory/           工作记忆 · 长期记忆接口
│   ├── mcp/              MCP stdio 客户端 · 远端工具发现
│   ├── tools/            内置工具 · ToolRegistry · 权限检查
│   ├── context/          Token 预算 · 前缀缓存 · 历史压缩
│   ├── loop/             Agent 状态机 · SubAgentRunner
│   ├── storage/          PostgreSQL 索引 · JSONL 日志
│   ├── runtime/          Session · Run · EventBus · Approval
│   ├── http/             REST API + SSE 流
│   ├── prompts/          System Prompt 模板
│   └── worldbuilding/    World · Agent · Narrative · Foreshadowing · Secret · Voice
├── cli/                  serve / tui 入口
├── webui/                React Web UI
├── config/prompts/       业务 Prompt 配置
├── scripts/              辅助脚本
└── docs/                 设计与领域文档
```

## 运行测试

```bash
cd build && ctest --output-on-failure

cd webui
npm run test
npm run lint
npm run build
```

## 技术栈

| 层级 | 技术 |
|------|------|
| 语言 | C++23 / TypeScript |
| 构建 | CMake + Conan 2 / Vite |
| HTTP Server | cpp-httplib |
| HTTP Client | libcurl |
| 存储 | PostgreSQL + JSONL |
| JSON | nlohmann/json |
| 日志 | spdlog |
| TUI | FTXUI |
| Web UI | React 19 + CSS Modules |
| 测试 | GTest / Vitest |

## License

[MIT](LICENSE)
