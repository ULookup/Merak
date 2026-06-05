# Merak

> 为长篇小说和世界观构建而生的 AI Agent 运行时。

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Merak 是一个可自部署的 AI Agent 服务。它在 LLM 之上提供了一套完整的写作工作流引擎——管理你的世界观、角色、伏笔和叙事结构，让 AI 成为真正理解你创作意图的写作伙伴。

**核心设计：**

- **分层 Agent 系统**：God（全知叙事者）、Manager（专业领域顾问）、Character（角色模拟）三层 Agent 各司其职，协作推进创作
- **叙事骨架**：Arc → Chapter → Section → Scene 四级结构，AI 永远知道你当前在写哪一章
- **伏笔生命周期**：每条伏笔有 Open → Paid → Abandoned 三态，关键节点自动提醒回收
- **信息不对称模型**：每个角色维护 Public / Secret / Unknown 三套知识边界，泄密自动检测
- **角色声音指纹**：分析句式、修饰词、签名词，保持角色说话风格稳定，不自动改写
- **角色记忆演化**：角色卡版本历史 + 日记 + 关系网 + 记忆摘要，角色随故事推进"成长"

**使用方式：**

本地启动服务端，终端客户端（TUI）或浏览器（Web UI）连接使用，体验一致。

**数据主权：** 全部数据落在本机 PostgreSQL + JSONL，`~/.merak/` 目录下管理。你的故事永远属于你。

---

## 为什么需要 Merak？

通用 AI 聊天工具在长篇创作中会暴露系统性问题，Merak 针对每一个做了专门设计：

| 痛点 | 表现 | Merak 的解决 |
|------|------|-------------|
| **角色遗忘** | 写到第三章，AI 忘了角色最初的性格和动机 | 角色卡版本历史 + 记忆摘要，每次对话自动注入角色当前状态 |
| **语言同质化** | 将军和公主说话一个腔调，旁白和角色内心独白分不清 | 角色声音指纹——分析每个角色的句式、修饰词、签名词，在上下文中锚定风格 |
| **世界观漂移** | 第一章说过"这个世界没有魔法"，第五章 AI 突然掏出了火球术 | 集中维护的 World 系统——地图、历史、魔法体系、势力关系在每次调用时作为锚定上下文注入 |
| **伏笔丢失** | 第三章埋了一条线索，写到第八章完全忘了这回事 | 伏笔生命周期管理——Open / Paid / Abandoned 三态追踪，关键节点提醒回收 |
| **情节跃进** | AI 为了快速收尾，跳过必要的铺垫直接跳到结局 | 叙事骨架约束——Arc → Chapter → Section → Scene 四级结构，AI 必须遍历当前节点才能推进 |
| **角色全知** | 反派莫名其妙知道了主角在另一个城市的秘密计划 | 信息不对称模型——每个角色维护 Public / Secret / Unknown 三套知识，泄密检测并标记 |
| **上下文稀释** | 百万字长篇后，AI 对新章节的理解越来越模糊 | Token 预算 + 前缀缓存 + 历史压缩，在上下文窗口内保留最相关的叙事信息 |
| **风格漂移** | 写到一半，AI 从严肃奇幻偏到了轻小说风格 | 项目级 System Prompt 固化创作基调，不随单次对话漂移 |

---

## 与 SillyTavern（酒馆）的区别

SillyTavern 是一个优秀的 AI 角色扮演前端——你和 AI 角色聊天互动。Merak 同样可以做到这一点。

区别在于：酒馆的聊天就是终点，而 Merak 的聊天是创作过程的一部分。

| | SillyTavern（酒馆） | Merak |
|---|---|---|
| **核心体验** | 和 AI 角色聊天互动 | 在完整世界观框架下和角色互动，产出的对话是小说的创作素材 |
| **角色的"记忆"** | 依赖 LLM 上下文窗口，切对话就忘 | 角色卡版本历史 + 记忆摘要 + 关系网，跨 Session 持续演化 |
| **世界观一致性** | Lorebook 手动维护，容易遗漏 | World 系统在每次 Agent 调用时自动注入锚定上下文，角色永远知道自己身在哪个世界 |
| **角色间协作** | 群聊模式（多个 AI 角色同台对话） | 分层 Agent 协作——God 掌控全局叙事，Manager 管理领域设定，Character 在约束下自主行动 |
| **信息不对称** | 无 | 每个角色维护 Public / Secret / Unknown 三套知识，泄密自动检测 |
| **叙事管理** | 无 | Arc → Chapter → Section → Scene 四级结构，伏笔生命周期追踪 |
| **输出产物** | 聊天记录 | 聊天记录 + 世界观文档 + 角色档案 + 结构化章节内容 |
| **适合场景** | 角色扮演、聊天互动 | 写小说、构建世界，同时可以和角色聊天来推进创作 |

---

## 架构一览

Merak 采用服务端-客户端分离架构。服务端负责所有 AI 能力（Agent Loop、工具、上下文、持久化），客户端只做输入和渲染。

```text
┌─────────────────────────────────────────┐
│              客  户  端                   │
│  ┌──────────┐          ┌──────────────┐ │
│  │  TUI     │          │   Web UI     │ │
│  │ (FTXUI)  │          │   (React)    │ │
│  └────┬─────┘          └──────┬───────┘ │
│       │    HTTP + SSE          │         │
└───────┼────────────────────────┼─────────┘
        │                        │
        ▼                        ▼
┌──────────────────────────────────────────┐
│             merak serve                   │
│  ┌─────────────────────────────────────┐ │
│  │  HTTP Layer — REST 路由 + SSE 推送   │ │
│  ├─────────────────────────────────────┤ │
│  │  Runtime — Session · Run · EventBus │ │
│  ├─────────────────────────────────────┤ │
│  │  AgentLoop — 状态机 · 工具循环 · 压缩│ │
│  ├─────────────────────────────────────┤ │
│  │  Context — Token 预算 · 前缀缓存     │ │
│  ├──────────┬──────────────────────────┤ │
│  │  Tools   │  Worldbuilding           │ │
│  │  内置+MCP│  World·Agent·Narrative   │ │
│  ├──────────┴──────────────────────────┤ │
│  │  Storage — PostgreSQL + JSONL       │ │
│  └─────────────────────────────────────┘ │
└──────────────────────────────────────────┘
```

**启动方式：**

```bash
# 启动服务端
merak serve

# 终端客户端
merak tui

# 或 Web UI（另起终端）
cd webui && npm run dev
```

TUI 和 Web UI 功能一致，连接同一个服务端，共享 Session 和 Run。

### 模块依赖

```text
merak-core            共享类型 · 错误定义
├── config            分层配置加载
├── llm               OpenAI / Anthropic Provider
├── mcp               MCP stdio 客户端
├── memory            工作记忆 · 长期记忆接口
├── storage           PostgreSQL 索引 · JSONL 日志
├── tools             内置工具 · MCP 动态注册
├── context           Token 预算 · 回压缩
├── loop              Agent 状态机 · 工具循环
├── runtime           Session · Run · EventBus · Approval
├── http              REST + SSE
├── worldbuilding     World · Agent · Narrative · Foreshadowing
└── cli               serve / tui 入口
```

---

## Worldbuilding 引擎

Worldbuilding 是 Merak 的核心领域引擎，专为长篇小说和世界观构建设计，包含六个子系统：

### 分层 Agent 架构

不是用一个大而全的 prompt 应付所有任务，而是三层 Agent 各司其职：

| Agent | 职责 | 示例 |
|-------|------|------|
| **God** | 全知叙事者，把控整体剧情走向、节奏和基调 | "第三章的高潮太弱了，需要再铺垫一章" |
| **Manager** | 专业领域顾问——地图、历史、魔法体系、势力关系 | "这个世界没有传送魔法，角色跨越大陆至少需要两周" |
| **Character** | 个体或群体角色模拟，在自身知识边界内行动 | 作为角色回应事件、更新日记、做出符合人设的决策 |

### 叙事骨架

强制性的四级结构，AI 永远知道自己当前在哪里：

```text
Arc（卷）
 └── Chapter（章）
      └── Section（节）
           └── Scene（场景）
```

AI 必须遍历当前节点才能推进，从架构层面防止情节跃进。每个节点可附加独立设定和上下文。

### 伏笔系统

| 状态 | 含义 |
|------|------|
| **Open** | 已埋下，等待回收 |
| **Paid** | 已回收，闭环完成 |
| **Abandoned** | 已废弃，记录原因 |

每进入新章节时自动检查未回收伏笔，提醒作者哪些线索该回收了。

### 信息不对称

角色的认知不是全知的。每个角色维护三套知识边界：

| 级别 | 含义 |
|------|------|
| **Public** | 角色知道，且知道别人也知道 |
| **Secret** | 角色知道，但知道别人不知道 |
| **Unknown** | 角色不知道 |

当角色在对话或行动中越界使用 Unknown 信息时，系统检测并标记为泄密事件。

### 角色声音指纹

防止语言同质化的机制，分析并锚定每个角色的表达特征：

- 句式偏好（长句/短句、疑问/感叹/陈述比例）
- 修饰词风格（形容词密度、常用副词）
- 签名词（角色特有的称呼、口头禅、语癖）

在 Character Agent 的 System Prompt 中注入，让角色保持稳定的说话风格。

### 角色记忆系统

角色不会"忘记"自己做过什么：

- **角色卡**：角色的核心设定，支持版本历史，记录设定变更
- **日记**：以角色第一人称记录关键事件和情感变化
- **关系网**：角色之间的社会关系，随情节演化
- **记忆摘要**：用上下文空间换角色一致性，每次对话自动注入角色当前应该记得的信息

---

## 快速开始

### 环境要求

| 依赖 | 版本 |
|------|------|
| GCC 或 Clang | GCC ≥ 13 / Clang ≥ 17 |
| CMake | ≥ 3.22 |
| Conan | ≥ 2.0 |
| PostgreSQL | ≥ 14 |

### 安装与构建

```bash
# 1. 安装 C++ 依赖
conan install . --build=missing -s build_type=Debug

# 2. 构建
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# 3. 初始化配置
./build/cli/merak --init
```

编辑 `~/.merak/settings.local.json`，填入 LLM API Key。

### 启动

```bash
# 终端1：启动服务端
./build/cli/merak serve

# 终端2：启动 TUI 客户端
./build/cli/merak tui

# 或者启动 Web UI
cd webui && npm install && npm run dev
# 打开 http://localhost:5173
```

恢复已有 Session：

```bash
./build/cli/merak tui --session <session_id>
```

### 运行测试

```bash
cd build && ctest --output-on-failure       # C++ 测试
cd webui && npm test                         # Web UI 测试
```

---

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
├── scripts/              部署脚本
└── tests/                CTest 注册
```

---

## Web UI

基于 React 19 + TypeScript + Vite 的单页应用，通过 HTTP + SSE 连接 `merak serve`。

```bash
cd webui
cp .env.example .env          # VITE_API_BASE 默认 http://localhost:3888
npm install && npm run dev
```

核心能力：Session 管理、聊天时间线、Markdown 渲染（含代码高亮）、工具面板、审批流程、SSE 连接状态监控、响应式布局、骨架屏加载。

---

## 文档索引

README 聚焦概览，详细内容见以下文档：

| 文档 | 内容 |
|------|------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 完整架构设计——模块详解、依赖关系、数据流 |
| [docs/RUNTIME.md](docs/RUNTIME.md) | Runtime 模型——Session / Run 生命周期、事件流、审批与取消 |
| [docs/AGENT_LOOP.md](docs/AGENT_LOOP.md) | Agent Loop——状态机、工具调用循环、安全机制 |
| [docs/HTTP_API.md](docs/HTTP_API.md) | HTTP API——端点列表、请求/响应格式、SSE 协议 |
| [docs/WORLDBUILDING.md](docs/WORLDBUILDING.md) | Worldbuilding 引擎——分层 Agent、叙事骨架、伏笔、声音指纹 |
| [docs/TOOLS.md](docs/TOOLS.md) | 工具系统——内置工具、权限模型、MCP 集成 |
| [docs/PERSISTENCE.md](docs/PERSISTENCE.md) | 持久化——PostgreSQL 索引、JSONL 日志、启动恢复 |
| [docs/TUI.md](docs/TUI.md) | TUI 客户端——命令参考、快捷键 |
| [webui/](webui/) | Web UI——技术栈、组件结构、开发指南 |

---

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

---

## 许可证

[MIT](LICENSE)
