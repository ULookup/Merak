<p align="center">
  <img src="assets/merak_logo_pure_vector.svg" alt="Merak Logo" width="520" />
</p>

<br />

<p align="center">
  <strong>Type Your World</strong><br />
  面向长篇小说创作的多 Agent 协作框架 &amp; Windows 桌面工作台
</p>

<p align="center">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat-square" />
  <img alt="React 19" src="https://img.shields.io/badge/React-19-149ECA?style=flat-square" />
  <img alt="Tauri 2" src="https://img.shields.io/badge/Tauri-2-24C8DB?style=flat-square" />
  <img alt="Windows" src="https://img.shields.io/badge/Windows-Desktop-0078D4?style=flat-square" />
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/License-MIT-blue.svg?style=flat-square" /></a>
</p>

---

## 什么是 Merak？

Merak 是一套给创作者使用的 **AI Agent 运行时与世界观构建工作台**。它把对话式 Agent、工具调用、角色卡、世界观设定、伏笔、秘密、章节结构、生成文件和本地工作区放进同一个连续创作流程里，让 AI 不只是回答问题，而是稳定地陪你从角色、背景、章节到场景，逐步搭建属于你的世界。

| 定位 | 说明 |
|---|---|
| 叙事引擎 | 围绕长篇叙事搭建的 worldbuilding engine，管理世界、角色、章节、场景、伏笔、秘密。 |
| 创作工作台 | 三栏 WebUI，可展示人物、世界、章节、场景和生成文件。 |
| 多 Agent 协作 | God Agent 统筹 → 工具调用 / Writer Agent 写作 / 知识图谱检索，SSE 实时推送。 |
| 本地优先 | 配置、会话、世界数据和输出文件均保存在本机；支持 SQLite + PostgreSQL + Neo4j。 |

## 快速开始

### 创作者：一键安装

1. 从 [Releases](https://github.com/ULookup/Merak/releases) 下载 Windows 安装包（`.msi` / `.exe`）
2. 安装后启动 Merak，进入设置页面配置 API Key 和模型
3. 新建世界，开始创作

### 开发者：从源码构建

**环境** — CMake 3.22+ · Conan 2.x · C++23 编译器 · Node.js

```bash
# 构建
conan install . --build=missing -s build_type=Debug
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# 初始化 & 启动
.\build\cli\Debug\merak.exe --init
.\build\cli\Debug\merak.exe serve

# WebUI（另开终端）
cd webui && npm install && npm run dev
```

访问 `http://127.0.0.1:5173`，Vite 会自动代理 `/v1/*` 和 `/api/*` 到 `127.0.0.1:3888`。

### Docker（仅开发数据库）

```bash
docker compose up -d
```

## 功能全景

### Agent 运行时

| 能力 | 说明 |
|---|---|
| Agent Loop | 多轮思考 → 工具调用 → 审批 → SSE 事件推送 |
| 工具体系 | 40+ 内置工具：文件、搜索、Git、LSP、Web、会话、记忆、Agent 调度 |
| MCP 支持 | stdio 协议接入外部工具，运行时导入 |
| 三级权限 | safe / ask / deny，Bash 五层安全检查 |

### 世界观构建

| 能力 | 说明 |
|---|---|
| 世界管理 | 创建、编辑、导入/导出 World，含世界观规则和背景设定 |
| 角色系统 | Agent 卡片（God / Manager / Character），含外貌、性格、背景、关系 |
| 叙事对象 | Arc → Chapter → Scene 层级结构，支持大纲和场景摘要 |
| 伏笔 & 秘密 | 伏笔追踪、揭示条件、秘密分级管理 |
| 创作 Pipeline | 五阶段流水线：方向选择 → 大纲 → 角色 → 场景写作 → 章节审校 |
| Voice 分析 | 角色语风一致性分析，防止角色跑偏 |

### 知识图谱

| 能力 | 说明 |
|---|---|
| Neo4j 后端 | 实体（角色/地点/组织/物品/概念）+ 15 种关系类型 |
| 关系提取 | LLM 自动抽取人物关系、站队、事实摘要 |
| 路径查询 | 子图浏览、邻居扩展、最短路径查找 |

### WebUI 工作台

| 区域 | 功能 |
|---|---|
| Sidebar | 世界/会话导航、模型配置、工具状态 |
| Run Timeline | 对话流、工具调用、审批、流式输出 |
| Composer | 场景/角色/世界规则/大纲/改写 快捷创作入口 |
| Inspector 面板 | Story / Files / Agents / Run 四栏审查 |

### Writer Agent

独立的 LLM 子循环，零工具注册，专注于从结构化材料包产出场景散文。与主 Agent 模型分离，可配置不同模型和写作风格。

## 架构一览

```mermaid
flowchart TB
  subgraph Desktop["Desktop（Tauri 2）"]
    WebUI["WebUI<br/>React 19"]
  end

  subgraph Server["merak serve"]
    HTTP["HTTP API<br/>/v1 + /api"]
    SSE["SSE Event Stream"]
    Runtime["RuntimeService<br/>Session / Run / Approval"]
    Loop["AgentLoop<br/>Think → Act → Observe → Respond"]
    Tools["ToolRegistry<br/>40+ 内置工具 + MCP"]
    World["WorldbuildingService<br/>World / Agent / Narrative / Secret"]
    KG["Knowledge Graph<br/>Neo4j"]
    Writer["Writer Agent<br/>独立 LLM 子循环"]
    Pipeline["PipelineManager<br/>5-stage workflow"]
    Storage["Storage<br/>SQLite / PostgreSQL / Files"]
  end

  WebUI --> HTTP
  HTTP --> Runtime
  Runtime --> SSE
  Runtime --> Loop
  Loop --> Tools
  Tools --> World
  Tools --> KG
  Tools --> Writer
  World --> Pipeline
  Runtime --> Storage
  World --> Storage
  KG --> Storage
```

| 层级 | 技术 |
|---|---|
| Language | C++23 · TypeScript · Rust |
| Build | CMake · Conan 2 · Vite · Tauri 2 |
| UI | React 19 · CSS Modules |
| HTTP | cpp-httplib · libcurl |
| JSON | nlohmann/json |
| Storage | SQLite · PostgreSQL + pgvector · Neo4j |
| Logging | spdlog |
| Testing | GTest · Vitest · Testing Library |

## 开发指南

### 配置 LLM

首次运行 `merak.exe --init` 后，编辑 `~\.merak\settings.local.json`：

```json
{
  "llm": {
    "provider": "openai",
    "api_key": "sk-your-api-key",
    "api_base_url": "https://api.openai.com/v1",
    "default_model": "gpt-4o",
    "max_output_tokens": 4096
  }
}
```

环境变量覆盖：`MERAK_PROVIDER` · `MERAK_API_KEY` · `MERAK_MODEL` · `MERAK_DB_CONNECTION`

### API 与事件

| 前缀 | 用途 |
|---|---|
| `/v1/*` | Runtime、Session、Run、Approval、SSE |
| `/api/*` | WebUI、Workspace Files、Worldbuilding |

### 数据目录

| 路径 | 说明 |
|---|---|
| `settings.local.json` | 本机私密配置（API Key） |
| `outputs/` | 默认输出文件 |
| `worlds/` | 世界观数据 |

> 配置优先级：环境变量 > 本地配置 > 用户配置 > 默认值

### Desktop 桌面端

Tauri 2 Windows 客户端，内嵌 WebUI 并启动本地 Merak 服务。

```bash
npm run desktop:dev     # 开发模式
npm run desktop:build   # 构建安装包
```

### PostgreSQL + Neo4j

worldbuilding 和 memory 依赖 PostgreSQL，知识图谱依赖 Neo4j。可通过 Docker 一键启动：

```bash
docker compose up -d
```

连接配置：

```json
{ "memory": { "db_connection": "postgresql://merak:password@127.0.0.1:5432/merak" } }
```

### 项目结构

```text
Merak/
├── apps/desktop/      Tauri 桌面壳
├── cli/               serve 命令入口 & 终端 UI
├── config/
│   ├── pipelines/     创作流水线配置
│   ├── prompts/       核心 prompt 与角色设定
│   └── skills/        内置创作技能
├── docs/              API、架构文档
├── libs/
│   ├── loop/          Agent 状态机主循环
│   ├── runtime/       Session / Run / Approval 管理
│   ├── context/       Token 预算 & 上下文组装
│   ├── tools/         40+ 内置工具 & 权限控制
│   ├── worldbuilding/ 世界观 & 叙事对象
│   ├── memory/        pgvector 记忆存储
│   ├── knowledge_graph/ Neo4j 知识图谱
│   ├── llm/           OpenAI / Anthropic provider
│   ├── http/          REST API + SSE
│   ├── mcp/           MCP stdio client
│   └── ...
├── tests/             C++ 测试入口
└── webui/             React 19 WebUI
```

## 参与贡献

我们欢迎任何形式的贡献——代码、文档、Bug 反馈、功能建议。

> 详见 [CONTRIBUTING.md](CONTRIBUTING.md) 和 [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)

## License

[MIT](LICENSE) · 版权 © 2025–2026 Merak contributors
