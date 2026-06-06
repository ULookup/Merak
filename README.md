<p align="center">
  <img src="webui/src/assets/merak-logo-lockup.svg" alt="Merak - Type Your World" width="420" />
</p>

<h1 align="center">Merak</h1>

<p align="center">
  <strong>Type Your World</strong><br />
  面向长篇小说、角色构建与世界观创作的 AI Agent 工作台。
</p>

<p align="center">
  <a href="#快速开始">快速开始</a>
  · <a href="#部署启动">部署启动</a>
  · <a href="#webui-工作台-windows">WebUI</a>
  · <a href="#tui-终端工作台">TUI</a>
  · <a href="#项目结构">项目结构</a>
</p>

<p align="center">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat-square" />
  <img alt="React 19" src="https://img.shields.io/badge/React-19-149ECA?style=flat-square" />
  <img alt="Windows" src="https://img.shields.io/badge/Windows-TUI%20%2B%20WebUI-0078D4?style=flat-square" />
  <img alt="Linux" src="https://img.shields.io/badge/Linux-TUI%20only-FCC624?style=flat-square" />
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/License-MIT-blue.svg?style=flat-square" /></a>
</p>

---

## 这是什么

Merak 不是一个“聊天壳”。它是一套给创作者使用的 AI 运行时：把对话、工具调用、角色卡、世界规则、伏笔、秘密、章节结构、生成文件与本地工作区放进同一个连续的创作流程里。

你可以把它理解为：

- 一个会写作、会查设定、会使用工具来辅助创作的 Agent
- 一个围绕长篇叙事搭建的 worldbuilding engine
- 一个能实时展示你构建的世界（包括人物、地图、场景等）的创作者工作台
- 一个把数据放在本地的小说工程系统

Merak 的核心目标很简单：让 AI 不只是回答问题，而是稳定地陪你从一个个角色和背景设定开始，搭建一个属于你的世界。

## 平台支持

| 平台 | TUI | WebUI | 说明 |
|---|---:|---:|---|
| Windows | Yes | Yes | 当前推荐平台。支持终端工作台和浏览器工作台。 |
| Linux | Yes | No | 当前仅支持 TUI。WebUI 暂不作为 Linux 支持目标。 |
| macOS | No | No | 当前未声明支持。 |

## 核心能力

| 能力 | 说明 |
|---|---|
| Agent Loop | 多轮思考、工具调用、审批、状态流转和 SSE 事件。 |
| WebUI 工作台 | 三栏创作者界面：左侧导航、中间 Run 时间线、右侧 Inspector。 |
| TUI 工作台 | 终端里的沉浸式对话与 worldbuilding 命令入口。 |
| Worldbuilding | World、Agent、Arc、Chapter、Scene、Foreshadowing、Secret、Voice。 |
| 文件产出 | Agent 可生成本地 Markdown/JSON/YAML/TXT 文件，WebUI 可浏览、预览和编辑。 |
| 本地数据 | Session、Run、世界观数据和输出文件优先落在本机。 |
| Provider 配置 | 支持 OpenAI-compatible 和 Anthropic-style provider 配置。 |
| MCP 与内置工具 | 文件、搜索、任务、世界观、会话等工具统一注册与权限控制。 |

## 为什么 Merak 不只是聊天

通用 AI 聊天工具适合灵感闪现，但很难稳定维护一个长篇故事。在我的观察中，很多朋友在创作时总是抱怨 AI 幻觉的问题。所以 Merak 把“创作上下文”当成核心来处理：

| 长篇创作问题 | 常见表现 | Merak 的处理方式 |
|---|---|---|
| 角色遗忘 | 写到后面动机、关系和经历漂移 | 角色卡、日记、关系、记忆摘要持续演化 |
| 声音同质化 | 所有人说话像同一个人 | 角色声音指纹与表达风格约束 |
| 世界规则漂移 | 魔法、历史、地理规则前后矛盾 | World/Manager Agent 维护领域设定 |
| 伏笔丢失 | 埋下线索却没有回收 | Open/Paid/Abandoned 生命周期追踪 |
| 信息越界 | 角色知道自己不该知道的秘密 | Public/Secret/Unknown 知识边界 |
| 情节断层 | AI 快速跳到结局，缺少过程 | Arc/Chapter/Section/Scene 叙事骨架 |
| 文件散落 | 草稿、设定、章节输出难管理 | Workspace Files 面板统一浏览和编辑 |

## 架构一览

```mermaid
flowchart TB
  subgraph Clients["Clients"]
    TUI["TUI<br/>Windows + Linux"]
    WebUI["WebUI<br/>Windows only"]
  end

  subgraph Server["merak serve"]
    HTTP["HTTP API<br/>/v1 + /api"]
    SSE["SSE Event Stream"]
    Runtime["RuntimeService<br/>Session / Run / Approval"]
    Loop["AgentLoop<br/>Thinking / Acting / Observing / Responding"]
    Tools["ToolRegistry<br/>Builtin tools + MCP tools"]
    World["WorldbuildingService<br/>World / Agent / Narrative / Secret"]
    Storage["Local Storage<br/>SQLite runtime + PostgreSQL/world files"]
  end

  TUI --> HTTP
  WebUI --> HTTP
  HTTP --> Runtime
  Runtime --> SSE
  Runtime --> Loop
  Loop --> Tools
  Tools --> World
  Runtime --> Storage
  World --> Storage
```

## 快速开始

如果你已经装好 CMake、Conan 和编译器，可以按下面的最短路径启动。

Windows PowerShell, TUI + WebUI:

```powershell
conan install . --build=missing -s build_type=Debug
cmake -B build `
  -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake `
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
.\build\cli\Debug\merak.exe --init
notepad "$env:USERPROFILE\.merak\settings.local.json"
.\build\cli\Debug\merak.exe serve
```

另开一个 PowerShell:

```powershell
cd webui
copy .env.example .env
npm install
npm run dev
```

然后打开:

```text
http://127.0.0.1:5173
```

Linux, TUI only:

```bash
conan install . --build=missing -s build_type=Debug
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/cli/merak --init
nano "$HOME/.merak/settings.local.json"
./build/cli/merak tui
```

## 部署启动

### 环境要求

| 依赖 | Windows | Linux |
|---|---|---|
| C++ compiler | MSVC 2022 或 Clang/LLVM | GCC 13+ 或 Clang 17+ |
| CMake | 3.22+ | 3.22+ |
| Conan | 2.x | 2.x |
| PostgreSQL | 14+ 或随包 portable pg | 14+ |
| Node.js | LTS, 仅 WebUI 需要 | 不需要, Linux 仅 TUI |

> 如果你只想跑 Linux TUI，不需要安装 Node.js，也不需要启动 WebUI。

### 1. 获取源码

```bash
git clone https://github.com/ULookup/Merak.git
cd Merak
```

### 2. 初始化 Conan profile

Windows PowerShell:

```powershell
conan profile detect --force
```

Linux:

```bash
conan profile detect --force
```

### 3. 安装 C++ 依赖

Windows PowerShell:

```powershell
conan install . --build=missing -s build_type=Debug
```

Linux:

```bash
conan install . --build=missing -s build_type=Debug
```

Conan 默认会把 toolchain 放到类似下面的位置：

```text
build/Debug/generators/conan_toolchain.cmake
```

如果你的 Conan layout 不同，请在 `build` 目录下搜索 `conan_toolchain.cmake`，然后把实际路径传给 CMake。

### 4. 配置 CMake

Windows PowerShell:

```powershell
cmake -B build `
  -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake `
  -DCMAKE_BUILD_TYPE=Debug
```

Linux:

```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug
```

### 5. 构建 Merak

Windows PowerShell:

```powershell
cmake --build build --config Debug
```

Linux:

```bash
cmake --build build -j
```

### 6. 初始化本地目录

Merak 使用 `~/.merak` 作为用户级配置和数据目录。

Windows PowerShell:

```powershell
.\build\cli\Debug\merak.exe --init
```

如果你的生成器不是 Visual Studio，二进制可能在：

```powershell
.\build\cli\merak.exe --init
```

Linux:

```bash
./build/cli/merak --init
```

### 7. 配置 LLM

创建或编辑 `settings.local.json`。

Windows PowerShell:

```powershell
notepad "$env:USERPROFILE\.merak\settings.local.json"
```

Linux:

```bash
nano "$HOME/.merak/settings.local.json"
```

最小配置示例：

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
    "enabled": true
  }
}
```

也可以用环境变量覆盖配置：

| 环境变量 | 作用 |
|---|---|
| `MERAK_PROVIDER` | Provider 名称 |
| `MERAK_API_KEY` | API Key |
| `MERAK_API_BASE_URL` | API Base URL |
| `MERAK_MODEL` | 默认模型 |
| `MERAK_DB_CONNECTION` | PostgreSQL 连接串 |
| `MERAK_PERMISSION_MODE` | 工具权限模式: `ask` / `auto` / `deny` |
| `MERAK_TUI_THEME` | TUI 主题 |

### 8. 启动服务端

Windows PowerShell:

```powershell
.\build\cli\Debug\merak.exe serve
```

Linux:

```bash
./build/cli/merak serve
```

默认服务地址：

```text
http://127.0.0.1:3888
```

## Windows: 启动 WebUI

> WebUI 当前只声明支持 Windows。

打开一个新的 PowerShell：

```powershell
cd webui
copy .env.example .env
npm install
npm run dev
```

默认访问：

```text
http://127.0.0.1:5173
```

Vite 会把 `/v1/*` 和 `/api/*` 代理到：

```text
http://127.0.0.1:3888
```

如果你的服务端不是默认地址，可以设置：

```powershell
$env:VITE_PROXY_TARGET="http://127.0.0.1:3888"
npm run dev
```

或者在 `webui/.env` 中填写：

```dotenv
VITE_API_BASE=http://127.0.0.1:3888
```

## TUI 终端工作台

Windows PowerShell:

```powershell
.\build\cli\Debug\merak.exe tui
```

Linux:

```bash
./build/cli/merak tui
```

恢复已有会话：

Windows PowerShell:

```powershell
.\build\cli\Debug\merak.exe tui --session <session_id>
```

Linux:

```bash
./build/cli/merak tui --session <session_id>
```

常用 worldbuilding 命令：

```text
/world list
/world create 北境
/world use <world_id>
/agent list
/agent create character 林霜
/story overview
/chapter new 雪夜来客
/scene new 旅店试探
/scene end
/foreshadow list
/secret list
/voice check
```

## WebUI 工作台 Windows

WebUI 是 Merak 的图形化创作者工作台。

| 区域 | 功能 |
|---|---|
| Sidebar | 世界、会话、模型、工具、上下文状态 |
| Run Timeline | 对话、工具调用、审批、流式输出 |
| Composer | Scene、Character、World Rule、Outline、Rewrite 快捷创作模式 |
| Story Inspector | 世界摘要、当前章节、场景、伏笔、秘密、角色知识边界 |
| Files Inspector | 本地输出文件列表、搜索、筛选、预览、编辑和保存 |
| Agents Inspector | God、Manager、Character 分组和角色卡摘要 |
| Run Inspector | Run 阶段、token、工具调用、SSE 状态和错误恢复提示 |

WebUI 依赖 `merak serve`。请先启动服务端，再启动 `npm run dev`。

## 数据目录

Merak 默认使用用户目录下的 `.merak`：

Windows:

```text
%USERPROFILE%\.merak
```

Linux:

```text
$HOME/.merak
```

常见内容：

| 路径 | 说明 |
|---|---|
| `settings.json` | 用户级通用配置 |
| `settings.local.json` | 本机私密配置, API Key 建议放这里 |
| `outputs/` | 默认输出文件目录 |
| `worlds/` | 世界观数据和世界输出 |
| runtime/session storage | 会话、Run、事件与审批记录 |

## PostgreSQL

Merak 的世界观和记忆能力依赖 PostgreSQL。你可以使用外部 PostgreSQL，也可以使用发行包中可能携带的 portable PostgreSQL。

外部 PostgreSQL 示例：

```json
{
  "memory": {
    "enabled": true,
    "db_connection": "postgresql://postgres:postgres@127.0.0.1:5432/merak"
  }
}
```

也可以使用环境变量：

Windows PowerShell:

```powershell
$env:MERAK_DB_CONNECTION="postgresql://postgres:postgres@127.0.0.1:5432/merak"
```

Linux:

```bash
export MERAK_DB_CONNECTION="postgresql://postgres:postgres@127.0.0.1:5432/merak"
```

## API 与事件

服务端 API 分为两组：

| 前缀 | 用途 |
|---|---|
| `/v1/*` | Runtime、Session、Run、Approval、SSE |
| `/api/*` | WebUI、配置、workspace files、worldbuilding |

常用入口：

| 接口 | 说明 |
|---|---|
| `GET /v1/runtime` | 获取模型、工具、MCP、worldbuilding 状态 |
| `POST /v1/sessions` | 创建会话 |
| `POST /v1/sessions/{id}/runs` | 启动一次 Run |
| `GET /v1/sessions/{id}/events/stream` | SSE 事件流 |
| `GET /api/webui/capabilities` | WebUI 后端能力声明 |
| `GET /api/workspace/files` | 工作区文件列表 |
| `GET /api/worldbuilding/worlds` | 世界列表 |
| `GET /api/worldbuilding/{world_id}/overview` | Story 总览 |

更多接口见：

- [docs/api-reference.md](docs/api-reference.md)
- [docs/webui-backend-requirements.md](docs/webui-backend-requirements.md)

## 项目结构

```text
Merak/
├── cli/                  serve / tui 入口
├── config/prompts/       Prompt 与 worldbuilding 角色设定
├── docs/                 API、设计、后端需求文档
├── libs/
│   ├── config/           配置加载与环境变量覆盖
│   ├── context/          Token budget、上下文压缩
│   ├── core/             共享类型和执行接口
│   ├── http/             REST API + SSE
│   ├── llm/              OpenAI / Anthropic provider
│   ├── loop/             Agent 状态机
│   ├── mcp/              MCP stdio client
│   ├── memory/           记忆接口
│   ├── portable_pg/      随包 PostgreSQL 生命周期管理
│   ├── prompts/          Prompt compositor
│   ├── runtime/          Session / Run / EventBus / Approval
│   ├── storage/          Session store 与运行时持久化
│   ├── tools/            内置工具、权限、工具注册
│   └── worldbuilding/    World / Agent / Narrative / Secret / Voice
├── scripts/              安装与辅助脚本
├── tests/                C++ 测试入口
└── webui/                React 19 WebUI, Windows 支持
```

## 开发命令

C++:

```bash
conan install . --build=missing -s build_type=Debug
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

WebUI, Windows:

```powershell
cd webui
npm install
npm run dev
npm run lint
npm run build
```

测试:

```bash
cd build
ctest --output-on-failure
```

## 常见问题

### WebUI 显示无法连接服务端

确认 `merak serve` 已经启动，并监听：

```text
http://127.0.0.1:3888
```

如果服务端端口不同，设置 `VITE_PROXY_TARGET` 或 `VITE_API_BASE`。

### CMake 找不到 Conan toolchain

先确认文件存在：

Windows PowerShell:

```powershell
Get-ChildItem -Recurse build -Filter conan_toolchain.cmake
```

Linux:

```bash
find build -name conan_toolchain.cmake
```

把找到的路径传给：

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<path-to-conan_toolchain.cmake>
```

### Linux 为什么没有 WebUI

当前支持的是：

- Windows: TUI + WebUI
- Linux: TUI only

Linux WebUI 暂不作为当前版本的支持目标，因为很少有人用Linux图形化界面（当然，如果你用的是图形化 Linux 系统，你也可以正常启动 WebUI）。

### 配置改了但没有生效

配置优先级从低到高：

1. 内置默认值
2. `~/.merak/settings.json`
3. `~/.merak/settings.local.json`
4. 项目 `.merak/settings.json`
5. 项目 `.merak/settings.local.json`
6. 环境变量

如果不确定当前加载了哪个配置，启动时查看终端输出中的 `Config: loaded ...`。

## 技术栈

| 层级 | 技术 |
|---|---|
| Language | C++23, TypeScript |
| Build | CMake, Conan 2, Vite |
| UI | FTXUI, React 19, CSS Modules |
| HTTP | cpp-httplib, libcurl |
| JSON | nlohmann/json |
| Storage | SQLite, PostgreSQL, local files |
| Logging | spdlog |
| Testing | GTest, Vitest |

## License

[MIT](LICENSE)
