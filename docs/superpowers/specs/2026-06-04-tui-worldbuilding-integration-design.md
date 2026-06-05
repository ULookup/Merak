# TUI Worldbuilding 集成 & 系统提示词强化 — 设计文档

## 概述

将 Worldbuilding 系统接入 TUI 和 Server，实现两者完整连通：
- TUI 斜杠命令直接操控 WorldbuildingService
- Agent 在对话中调用世界构建工具
- 三类 Agent（创作调度员、领域管理员、角色）各自拥有强化的系统提示词
- 利用现有子 Agent 机制，动态加载运行时创建的角色提示词

## 架构

```
run_server() 启动时:
  ├── ConfigLoader::load()
  ├── LLM Provider
  ├── ToolRegistry
  │    ├── 基础工具（ReadFile, Bash, Grep...）
  │    └── WorldbuildingTools（按 AgentKind 注册）
  ├── WorldbuildingService（新增实例化）
  │    └── PostgreSQL 连接
  ├── HttpServer
  │    ├── /api/sessions/*           （现有）
  │    ├── /api/approvals/*          （现有）
  │    └── /api/worldbuilding/*      （新增）
  └── RuntimeService
       └── 持有 WorldbuildingService 引用
```

## 第一部分：系统提示词

### 创作调度员 (Creative Director)

硬编码在代码中，最高权限。创建角色/管理者时，为其编写系统提示词并存入 AgentStore。

```
身份：你是这个虚构世界的创作调度员（Creative Director），拥有最高创作权限。

你能使用的工具：
- ReadCharacterCard / CreateCharacter / SearchAgent — 角色管理
- ReadSecret / ExposeSecret — 秘密管理
- ReadForeshadowing / PlantForeshadowing / ListOpenForeshadowing — 伏笔管理
- QueryWorld / AdvanceWorldTime — 世界管理
- EndScene / QueryHistory / QueryMap / QueryMagic / QueryFaction — 叙事与领域管理
- UpdateAgentPrompt — 更新角色/管理者的系统提示词

工作流程：
- 创建角色时：先写完整 CharacterCard → 再调用 UpdateAgentPrompt 为其编写系统提示词
- 创建管理者时：先定义领域职责和知识 → 再调用 UpdateAgentPrompt 为其编写系统提示词
- 结束场景时：调用 EndScene，系统会自动更新角色日记、关系和声音特征

创作原则：
- 一致性：所有设定必须自洽
- 因果链：每个事件都有前因后果，伏笔必须有回收计划
- 角色驱动：情节由角色内在欲望和恐惧推动
```

### 领域管理员 (Domain Manager)

```
身份：你是世界"{world_name}"的 {role} 管理者。

你能使用的工具：
- Query{domain} — 查询 {domain} 领域数据
- {按 ManagerKind 列出具体工具，如 MapManager → QueryMap}

你管理的文件/数据：
- {domain} 领域的所有设定数据

规则：
- 只回答领域内问题
- 引用已有设定时标注来源
- 如果信息不存在，如实告知，不要编造
```

### 角色 (Character)

```
身份：你是 {character_name}，{identity}。
性格：{traits}，欲望：{desires}，恐惧：{fears}
声音特征：{voice_style}

你能使用的工具：
- DescribeCharacter — 描述其他角色的外貌
- SearchMyDiary — 搜索自己的日记
- LookAround — 查看当前位置、在场角色、世界时间

你的个人档案：
- CharacterCard：你的完整角色设定
- Diary：你的日记（由 EndScene 自动生成）
- Relations：你与其他角色的关系图谱
- Voice：你的声音指纹

当前所在：{location}，世界时间 {world_time}

规则：
- 始终以角色身份说话，不要跳出角色
- 知识仅限于角色应该知道的范围
- 反应符合性格和情绪状态

写日记规则。以下情况你应该主动写日记：
- 当前场景结束，或你感知到场景即将切换
- 你经历了强烈情绪（喜悦、悲伤、愤怒、恐惧、惊讶）
- 你与其他角色发生了重要互动（冲突、表白、约定、背叛）
- 你获取了重要信息或发现了秘密
- 你的关系或处境发生了实质变化
- 你做了一个重要的决定

写日记时：
- 以第一人称书写，使用你角色的声音特征
- 记录发生了什么、你的感受和想法
- 日记是你私人的——写出真实想法，不需要对任何人表演
- 日记写入后会自动保存，你可以通过 SearchMyDiary 随时查阅
```

`{...}` 内变量由创作调度员在创建 Agent 时填入。提示词存储在 AgentStore 的 PostgreSQL 中。

## 第二部分：Server 端集成

### WorldbuildingService 实例化

`main.cpp` 的 `run_server()` 中：

```cpp
auto worldbuilding_service = std::make_shared<WorldbuildingService>(
    pg_conninfo, cfg.storage.fs_root);
worldbuilding_service->initialize();
```

### 新增 UpdateAgentPromptTool

只有创作调度员能调用：

- agent_kind: CreativeDirector
- permission: Ask（修改角色提示词需要作者确认）
- 输入：agent_id, new_system_prompt
- 实现：调用 AgentStore::update_agent_prompt(agent_id, prompt)

### AgentStore 新增方法

```cpp
void update_agent_prompt(agent_id, prompt);   // 创作调度员写入
std::string load_agent_prompt(agent_id);       // Runtime 启动角色 Agent 时加载
```

### Agent 工具注册

启动时按 Agent 类型注册对应工具：

- 创作调度员：全部世界构建工具
- 领域管理员：对应领域的 Query 工具
- 角色：DescribeCharacter、SearchMyDiary、LookAround

### HTTP API 端点

新增路由（`execute_worldbuilding_command` 已定义路径）：

| 前缀 | 功能 |
|------|------|
| `/api/worldbuilding/worlds` | 世界 CRUD |
| `/api/worldbuilding/{wid}/agents` | 角色 CRUD |
| `/api/worldbuilding/{wid}/story` | 故事概览 |
| `/api/worldbuilding/{wid}/chapters` | 章节管理 |
| `/api/worldbuilding/{wid}/arcs` | 弧线管理 |
| `/api/worldbuilding/{wid}/scenes` | 场景管理 |
| `/api/worldbuilding/{wid}/time` | 时间管理 |
| `/api/worldbuilding/{wid}/foreshadowing` | 伏笔管理 |
| `/api/worldbuilding/{wid}/secrets` | 秘密管理 |
| `/api/worldbuilding/{wid}/voice` | 声音分析 |
| `/api/worldbuilding/{wid}/memory` | 角色记忆/日记 |

实现为 `WorldbuildingHttpHandler`，持有 WorldbuildingService 引用，注册到 HttpServer。

## 第三部分：TUI 集成

### 斜杠命令连接

`main.cpp` 的 `on_command` handler 中新增：

```cpp
auto wb_cmd = parse_worldbuilding_command(input);
if (wb_cmd) {
    execute_worldbuilding_command(*wb_cmd, [&](auto method, auto path, auto body) {
        return runtime_client_->request(method, path, body);
    });
    return;
}
```

### 显示方式

世界构建命令结果以 `SystemCell` 内联显示在时间线中：

- 列表类：双栏表格
- 详情类：分区卡片，支持折叠/展开
- 操作结果：简短确认消息
- 错误：红色错误消息

## 第四部分：子 Agent 机制利用

### 现有机制

RuntimeService 已有完整子 Agent 系统：SubAgentConfig、start_delegation（fan_out/sequential/pipeline）、独立 AgentLoop、事件流、HTTP API、TUI `/team` 命令。

### 集成方案

创作调度员通过 delegation 调度角色/管理者：

```
用户: @叶霜 你对刚才的事情怎么看？

  → RuntimeService 收到请求
  → 从 AgentStore 加载 "叶霜" 的 system_prompt
  → 构造 SubAgentConfig{
        id: "叶霜",
        system_prompt: <从AgentStore加载>,
        tool_allowlist: ["DescribeCharacter", "SearchMyDiary", "LookAround"]
    }
  → start_delegation(pattern="fan_out", agents=["叶霜"], task="...")
  → 现有子 Agent 机制接管
  → 角色以角色身份回复
```

多管理 Agent 并行查询：

```
创作调度员需要查询世界信息:
  → delegation: fan_out to [MapManager, HistoryManager, MagicManager]
  → 各管理 Agent 并行查询
  → 聚合结果返回
```

## 文件变更清单

| 文件 | 变更 |
|------|------|
| `libs/worldbuilding/src/prompts/` | 新增：三类 Agent 系统提示词常量 |
| `libs/worldbuilding/src/worldbuilding_tools.cpp` | 新增：UpdateAgentPromptTool |
| `libs/worldbuilding/src/agent_store.cpp` | 新增：update_agent_prompt / load_agent_prompt |
| `libs/worldbuilding/src/worldbuilding_service.cpp` | 新增：update_agent_prompt 透传 |
| `libs/http/src/worldbuilding_handler.cpp` | 新增：WorldbuildingHttpHandler |
| `cli/src/main.cpp` | 修改：实例化 WorldbuildingService、注册 WorldbuildingTools、注册 HTTP 路由、TUI 路由世界构建命令 |
| `cli/src/tui/history_cell/history_cell.hpp` | 修改：SystemCell 支持世界构建结果渲染 |
