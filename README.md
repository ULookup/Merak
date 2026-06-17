<p align="center">
  <img src="webui/src/assets/merak-logo-lockup.svg" alt="Merak - Type Your World" width="420" />
</p>

<h1 align="center">Merak</h1>

<p align="center">
  <strong>Type Your World</strong><br />
  面向长篇小说、角色构建与世界观创作的 Windows 桌面端 AI Agent 工作台。
</p>

<p align="center">
  <a href="#快速开始">快速开始</a>
  · <a href="#5-分钟创作上手">5 分钟创作上手</a>
  · <a href="#运行形态">运行形态</a>
  · <a href="#webui-工作台">WebUI</a>
  · <a href="#项目结构">项目结构</a>
</p>

<p align="center">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat-square" />
  <img alt="React 19" src="https://img.shields.io/badge/React-19-149ECA?style=flat-square" />
  <img alt="Tauri 2" src="https://img.shields.io/badge/Tauri-2-24C8DB?style=flat-square" />
  <img alt="Windows" src="https://img.shields.io/badge/Windows-Desktop-0078D4?style=flat-square" />
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/License-MIT-blue.svg?style=flat-square" /></a>
</p>

---

## 这是什么？

Merak 不是一个普通聊天壳。它是一套给创作者使用的 AI 运行时和工作台，把对话、工具调用、角色卡、世界规则、伏笔、秘密、章节结构、生成文件和本地工作区放进同一个连续的创作流程里。

你可以把它理解为：

- 一个会写作、会查设定、会调用工具辅助创作的 Agent。
- 一个围绕长篇叙事搭建的 worldbuilding engine。
- 一个可以展示人物、世界、章节、场景和生成文件的创作者工作台。
- 一个本地优先的小说工程系统，配置、输出和世界观数据都尽量落在你的机器上。

Merak 是一个 Windows 桌面端程序（Tauri 2），WebUI 嵌入桌面壳中运行，提供完整的创作工作台体验。

Merak 的核心目标很简单：让 AI 不只是回答问题，而是稳定地陪你从角色、背景、章节、伏笔和场景开始，逐步搭建属于你的世界。

## 平台支持

| 平台 | 形态 | 说明 |
|---|---|---|---|
| Windows | Desktop（Tauri 2） | 推荐平台。 |

## 运行形态

| 形态 | 入口 | 用途 |
|---|---|---|
| Runtime server | `merak serve` | 提供 `/v1/*` 和 `/api/*` HTTP/SSE 接口。 |
| WebUI | `webui` Vite app | 浏览器中的三栏创作者工作台。 |
| Desktop | `apps/desktop` Tauri app | Windows 桌面工作台；启动本地 Merak 服务并打包 WebUI。 |

## 核心能力

| 能力 | 说明 |
|---|---|
| Agent loop | 多轮思考、工具调用、审批、状态流转和 SSE 事件输出。 |
| Runtime API | Session、Run、Approval、workspace files、worldbuilding 等 HTTP 接口。 |
| WebUI 工作台 | 左侧世界/会话导航，中间 Run 时间线，右侧 Story、Files、Agents、Run inspectors。 |
| Worldbuilding | World、Agent、Arc、Chapter、Scene、Foreshadowing、Secret、Voice 等叙事对象。 |
| 本地文件产出 | Agent 可生成 Markdown、JSON、YAML、TXT 等文件，WebUI 可浏览、预览和编辑。 |
| 本地数据 | 配置、session、run、世界观数据和输出文件优先保存在本机。 |
| Provider 配置 | 支持 OpenAI-compatible 和 Anthropic-style provider 配置。 |
| MCP 与内置工具 | 文件、搜索、任务、世界观、会话等工具统一注册与权限控制。 |

## 架构一览

```mermaid
flowchart TB
  subgraph Clients["Desktop（Tauri 2）"]
    WebUI["WebUI<br/>Browser (embedded)"]
  end

  subgraph Server["merak serve"]
    HTTP["HTTP API<br/>/v1 + /api"]
    SSE["SSE Event Stream"]
    Runtime["RuntimeService<br/>Session / Run / Approval"]
    Loop["AgentLoop<br/>Thinking / Acting / Observing / Responding"]
    Tools["ToolRegistry<br/>Builtin tools + MCP tools"]
    World["WorldbuildingService<br/>World / Agent / Narrative / Secret"]
    KG["Knowledge Graph<br/>Neo4j provider"]
    Writer["Writer Agent<br/>Independent LLM loop"]
    Pipeline["PipelineManager<br/>5-stage workflow"]
    Storage["Local Storage<br/>SQLite / PostgreSQL / files"]
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

## 快速开始

下面是从源码启动 Merak 的最短路径。你需要先安装 CMake、Conan 2、C++23 编译器，以及 WebUI 所需的 Node.js。

### Windows PowerShell

```powershell
conan profile detect --force
conan install . --build=missing -s build_type=Debug
cmake -B build `
  -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake `
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug

.\build\cli\Debug\merak.exe --init
notepad "$env:USERPROFILE\.merak\settings.local.json"
.\build\cli\Debug\merak.exe serve
```

另开一个 PowerShell 启动 WebUI：

```powershell
cd webui
copy .env.example .env
npm install
npm run dev
```

然后打开：

```text
http://127.0.0.1:5173
```

## 5 分钟创作上手

安装完成后，按以下五步开始你的第一次创作：

### 1. 初始化配置

```powershell
.\build\cli\Debug\merak.exe --init
```

这会生成 `~\.merak\` 目录，包含默认配置文件。

### 2. 配置模型

打开 Desktop 程序，进入设置页面填入 API Key 和模型名。或者直接编辑 `~\.merak\settings.local.json`：

```json
{
  "llm": {
    "provider": "openai",
    "api_key": "sk-your-api-key",
    "api_base_url": "https://api.openai.com/v1",
    "default_model": "gpt-4o"
  }
}
```

### 3. 创建世界

在 Desktop 工作台左侧 Sidebar 点击"新建世界"，输入世界名称和世界观描述。例如：

> 这是一个架空的东方玄幻世界，名为"苍云大陆"。修行者通过吸纳天地灵气突破境界，共分九境。各大宗门明争暗斗，凡人国度在夹缝中求生存。

### 4. 写第一个场景

1. 在 Agents 面板创建第一个角色（例如"林霜"，一位初入宗门的少女剑修）
2. 创建第一章"雪夜来客"
3. 在 Composer 输入场景描述，让 AI 开始创作

### 5. 查看产出

- **Story Inspector**：查看世界摘要、章节、场景叙事
- **Files Inspector**：浏览 AI 生成的文件（Markdown、JSON 等）
- **Agents Inspector**：查看角色卡和关系图谱

> 💡 想快速体验？可以导入示范项目 `examples/demo_world.json`，内含预置世界观、角色和场景。

## 环境要求

| 依赖 | 版本/说明 |
|---|---|
| C++ compiler | MSVC 2022、GCC 13+ 或 Clang 17+。 |
| CMake | 3.22+。 |
| Conan | 2.x。 |
| PostgreSQL | 14+；worldbuilding 和 memory 能力使用。 |
| Node.js | LTS；仅 WebUI/Desktop 开发需要。 |
| Rust | Tauri desktop 构建需要。 |

Conan 默认会把 toolchain 放到类似下面的位置：

```text
build/Debug/generators/conan_toolchain.cmake
```

如果你的 Conan layout 不同，请在 `build` 目录下搜索 `conan_toolchain.cmake`，再把实际路径传给 CMake。

## 配置 LLM

Merak 使用 `~/.merak` 作为用户级配置和数据目录。首次运行：

```powershell
.\build\cli\Debug\merak.exe --init
```

或：

```bash
./build/cli/merak --init
```

编辑 `settings.local.json`：

```json
{
  "llm": {
    "provider": "openai",
    "api_key": "sk-your-api-key",
    "api_base_url": "https://api.openai.com/v1",
    "default_model": "gpt-4o",
    "max_output_tokens": 4096
  },
  "agent": {
    "permission_mode": "ask",
    "max_tool_turns": 25
  },
  "memory": {
    "enabled": true,
    "db_connection": "postgresql://merak:merak123@127.0.0.1:5432/merak"
  }
}
```

也可以用环境变量覆盖配置：

| 环境变量 | 作用 |
|---|---|
| `MERAK_PROVIDER` | Provider 名称。 |
| `MERAK_API_KEY` | API Key。 |
| `MERAK_API_BASE_URL` | API Base URL。 |
| `MERAK_MODEL` | 默认模型。 |
| `MERAK_DB_CONNECTION` | PostgreSQL 连接串。 |
| `MERAK_PERMISSION_MODE` | 工具权限模式，例如 `ask`、`auto`、`deny`。 |

## WebUI 工作台

WebUI 依赖 `merak serve`。先启动服务端：

```powershell
.\build\cli\Debug\merak.exe serve
```

再启动前端：

```powershell
cd webui
npm install
npm run dev
```

默认访问地址：

```text
http://127.0.0.1:5173
```

Vite 默认把 `/v1/*` 和 `/api/*` 代理到：

```text
http://127.0.0.1:3888
```

如果服务端地址不同，可以设置：

```powershell
$env:VITE_PROXY_TARGET="http://127.0.0.1:3888"
npm run dev
```

或在 `webui/.env` 中写入：

```dotenv
VITE_API_BASE=http://127.0.0.1:3888
```

WebUI 主要区域：

| 区域 | 功能 |
|---|---|
| Sidebar | 世界、会话、模型、工具和上下文状态。 |
| Run Timeline | 对话、工具调用、审批、流式输出和运行状态。 |
| Composer | Scene、Character、World Rule、Outline、Rewrite 等快捷创作模式。 |
| Story Inspector | 世界摘要、章节、场景、伏笔、秘密和角色知识边界。 |
| Files Inspector | 本地输出文件列表、搜索、筛选、预览、编辑和保存。 |
| Agents Inspector | God、Manager、Character 分组和角色卡摘要。 |
| Run Inspector | Run 阶段、token、工具调用、SSE 状态和错误恢复提示。 |

## Desktop 桌面端

Desktop 是一个 Tauri 2 Windows 客户端，用来把 WebUI 放进桌面窗口，并在本地启动 Merak 服务。它会准备本地资料目录、选择可用端口、连接 WebUI，并在失败时提供重新打开和导出报告入口。

从仓库根目录启动开发模式：

```powershell
npm install
npm --prefix webui install
npm --prefix apps/desktop install
npm run desktop:dev
```

构建 Windows 安装包：

```powershell
npm run desktop:check
npm run desktop:build
```

Tauri 配置当前面向 Windows MSI 和 NSIS 安装包。打包前需要 Rust/Cargo、Windows C++ 构建工具，以及已构建的 `merak.exe`；也可以通过 `MERAK_RUNTIME_EXE` 指向已有运行时。

## 数据目录

默认用户目录：

| 平台 | 路径 |
|---|---|
| Windows | `%USERPROFILE%\.merak` |
| Linux | `$HOME/.merak` |

常见内容：

| 路径 | 说明 |
|---|---|
| `settings.json` | 用户级通用配置。 |
| `settings.local.json` | 本机私密配置，API Key 建议放这里。 |
| `outputs/` | 默认输出文件目录。 |
| `worlds/` | 世界观数据和世界输出。 |
| runtime/session storage | 会话、Run、事件与审批记录。 |

配置优先级通常是：内置默认值、用户配置、用户本地配置、项目配置、项目本地配置、环境变量。环境变量优先级最高。

## PostgreSQL

Merak 的 worldbuilding 和 memory 能力依赖 PostgreSQL。你可以使用外部 PostgreSQL，也可以通过 Docker 启动开发数据库。

`docker-compose.yml` 已包含一个开发用 PostgreSQL 服务，默认用户为 `merak`，默认密码为 `merak123`。也可以直接运行：

```bash
docker compose up -d
```

连接串示例：

```json
{
  "memory": {
    "enabled": true,
    "db_connection": "postgresql://merak:merak123@127.0.0.1:5432/merak"
  }
}
```

环境变量示例：

```powershell
$env:MERAK_DB_CONNECTION="postgresql://merak:merak123@127.0.0.1:5432/merak"
```

```bash
export MERAK_DB_CONNECTION="postgresql://merak:merak123@127.0.0.1:5432/merak"
```

## API 与事件

服务端 API 分为两组：

| 前缀 | 用途 |
|---|---|
| `/v1/*` | Runtime、Session、Run、Approval、SSE。 |
| `/api/*` | WebUI、配置、workspace files、worldbuilding。 |

常用入口：

| 接口 | 说明 |
|---|---|
| `GET /v1/runtime` | 获取模型、工具、MCP 和 worldbuilding 状态。 |
| `POST /v1/sessions` | 创建会话。 |
| `POST /v1/sessions/{id}/runs` | 启动一次 Run。 |
| `GET /v1/sessions/{id}/events/stream` | 订阅 SSE 事件流。 |
| `GET /api/webui/capabilities` | WebUI 后端能力声明。 |
| `GET /api/workspace/files` | 工作区文件列表。 |
| `GET /api/worldbuilding/worlds` | 世界列表。 |
| `GET /api/worldbuilding/{world_id}/overview` | Story 总览。 |

更多接口见：

- [docs/api-reference.md](docs/api-reference.md)
- [docs/webui-backend-requirements.md](docs/webui-backend-requirements.md)

## 项目结构

```text
Merak/
├── apps/desktop/          Tauri desktop shell
├── cli/                   serve 命令入口
├── config/
│   ├── pipelines/         创作 pipeline 配置
│   ├── prompts/           核心 prompt、规则和 worldbuilding 角色设定
│   └── skills/            Merak 内置创作技能
├── docs/                  API、设计和实现文档
├── libs/
│   ├── config/            配置加载与环境变量覆盖
│   ├── context/           Token budget 和上下文压缩
│   ├── context_dsl/       上下文 DSL
│   ├── core/              共享类型和执行接口
│   ├── http/              REST API + SSE
│   ├── knowledge_graph/   知识图谱能力
│   ├── llm/               OpenAI / Anthropic provider
│   ├── loop/              Agent 状态机
│   ├── mcp/               MCP stdio client
│   ├── memory/            记忆接口
│   ├── portable_pg/       随包 PostgreSQL 生命周期管理
│   ├── prompts/           Prompt compositor
│   ├── runtime/           Session / Run / EventBus / Approval
│   ├── skills/            技能加载与执行
│   ├── storage/           Session store 与运行时持久化
│   ├── tools/             内置工具、权限和工具注册
│   └── worldbuilding/     World / Agent / Narrative / Secret / Voice
├── scripts/               安装与开发辅助脚本
├── tests/                 C++ 测试入口
└── webui/                 React 19 WebUI
```

## 开发命令

C++：

```bash
conan install . --build=missing -s build_type=Debug
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

WebUI：

```bash
cd webui
npm install
npm run dev
npm run lint
npm run test
npm run build
```

Desktop：

```bash
npm install
npm --prefix webui install
npm --prefix apps/desktop install
npm run desktop:dev
npm run desktop:check
npm run desktop:build
```

C++ tests：

```bash
cd build
ctest --output-on-failure
```

## 常见问题

### WebUI 显示无法连接服务端

确认 `merak serve` 已启动，并监听：

```text
http://127.0.0.1:3888
```

如果服务端端口不同，设置 `VITE_PROXY_TARGET` 或 `VITE_API_BASE`。

### CMake 找不到 Conan toolchain

先确认文件存在：

```powershell
Get-ChildItem -Recurse build -Filter conan_toolchain.cmake
```

或：

```bash
find build -name conan_toolchain.cmake
```

然后把找到的路径传给：

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<path-to-conan_toolchain.cmake>
```

### 配置改了但没有生效

检查是否被更高优先级的项目配置或环境变量覆盖。启动时也可以查看终端输出里的 `Config: loaded ...` 日志。

### Desktop 打开后仍提示后端不可用

当前 desktop shell 不负责启动 runtime。请先启动：

```powershell
.\build\cli\Debug\merak.exe serve
```

再运行：

```powershell
npm run desktop:dev
```

## 技术栈

| 层级 | 技术 |
|---|---|
| Language | C++23, TypeScript, Rust |
| Build | CMake, Conan 2, Vite, Tauri 2 |
| UI | React 19, CSS Modules |
| HTTP | cpp-httplib, libcurl |
| JSON | nlohmann/json |
| Storage | SQLite, PostgreSQL, local files |
| Logging | spdlog |
| Testing | GTest, Vitest, Testing Library |

## License

[MIT](LICENSE)
