# Contributing to Merak

感谢你对 Merak 的关注！本文档帮助你了解如何参与贡献。

## 目录

- [行为准则](#行为准则)
- [如何报告问题](#如何报告问题)
- [如何提出新功能](#如何提出新功能)
- [开发环境搭建](#开发环境搭建)
- [开发流程](#开发流程)
- [Commit 规范](#commit-规范)
- [分支策略](#分支策略)
- [代码风格](#代码风格)
- [测试规范](#测试规范)
- [Pull Request 流程](#pull-request-流程)
- [Code Review 标准](#code-review-标准)

## 行为准则

本项目采用 [Contributor Covenant 2.1](CODE_OF_CONDUCT.md)。参与即视为同意遵守。

## 如何报告问题

1. 先在 [Issues](../../issues) 中搜索是否已有相同问题
2. 使用 Bug Report 模板提交，包含：
   - Merak 版本号
   - 操作系统和版本
   - 复现步骤
   - 期望行为 vs 实际行为
   - 相关日志或截图

安全问题请勿公开提交 Issue，通过私密渠道联系维护者。

## 如何提出新功能

1. 先在 Issues 中搜索是否已有类似提议
2. 使用 Feature Request 模板，说明：
   - 使用场景
   - 期望的解决方案
   - 备选方案（如果有）
3. 标记 `enhancement` 标签

## 开发环境搭建

### 前置要求

| 工具 | 最低版本 | 用途 |
|---|---|---|
| CMake | 3.28 | C++ 构建 |
| Conan | 2.x | C++ 依赖管理 |
| GCC / Clang / MSVC | C++23 支持 | 编译器 |
| Node.js | 22 | WebUI / Desktop |
| Rust | 1.80+ | Tauri Desktop |
| PostgreSQL | 16 | 数据存储（可选） |

### C++ 后端

```bash
# 安装依赖
conan install . --build=missing -s build_type=Debug

# 配置
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build build -j

# 运行
./build/cli/Debug/merak serve --port 3888
```

### WebUI（React 19）

```bash
cd webui
npm install
npm run dev       # 开发模式
npm run lint      # 代码检查
npm run test      # 运行测试
npm run build     # 生产构建
```

### Desktop（Tauri 2）

```bash
npm install
npm --prefix webui install
npm --prefix apps/desktop install
npm run desktop:dev    # 开发模式
npm run desktop:check  # 类型检查
npm run desktop:build  # 打包
```

### 运行测试

```bash
# C++ 测试
cd build && ctest --output-on-failure

# WebUI 测试
cd webui && npm run test
```

## 开发流程

1. Fork 本仓库
2. Clone 你的 Fork
3. 创建功能分支（见[分支策略](#分支策略)）
4. 开发并自测
5. 确保 CI 通过
6. 提交 PR

## Commit 规范

采用 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/v1.0.0/)：

```
<type>(<scope>): <subject>

<body>
```

### Type

| Type | 说明 |
|---|---|
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `docs` | 文档更新 |
| `style` | 代码格式（不影响逻辑） |
| `refactor` | 重构（非功能变更） |
| `perf` | 性能优化 |
| `test` | 测试相关 |
| `chore` | 构建/工具/依赖变更 |
| `ci` | CI 配置变更 |

### Scope

```
worldbuilding  worldbuilding 引擎
runtime        会话、运行、事件
http           REST API / SSE
llm            LLM Provider
memory         记忆系统
tools          工具系统
webui          前端工作台
desktop        Tauri 桌面端
config         配置系统
kg             知识图谱
skills         技能系统
cli            CLI 入口
```

### 示例

```
feat(worldbuilding): add timeline event recording endpoint

POST /api/worldbuilding/:wid/timeline 用于手动记录时间线事件。
```

## 分支策略

- `main` — 稳定分支，随时可发布
- `feat/<name>` — 新功能
- `fix/<name>` — Bug 修复
- `docs/<name>` — 文档

永远基于 `main` 最新提交创建分支。

## 代码风格

### C++

- C++23 标准
- 遵循项目现有代码结构
- 头文件使用 `#pragma once`
- 命名空间：`merak` 及其子命名空间

### TypeScript / React

- 严格模式
- 优先使用函数组件 + Hooks
- CSS Modules 管理样式
- 通过 `npm run lint` 检查

### Rust (Tauri)

- 遵循 `cargo clippy` 建议
- 保持 Tauri command 轻量

## 测试规范

- Bug 修复必须补充回归测试
- 新功能应有对应的单元测试或集成测试
- C++ 测试位于 `tests/` 和各 `libs/*/tests/`
- WebUI 测试位于 `webui/src/__tests__/`

## Pull Request 流程

1. 确保分支与 `main` 同步 (`git rebase main`)
2. 你的 PR 应该：
   - 通过所有 CI 检查
   - 如果是 UI 变更，附带截图
   - 参考已有 PR 模板填写说明
3. 至少一位维护者 Review 通过后方可合并
4. 合并使用 Squash Merge

## Code Review 标准

Reviewer 会关注：

- 功能是否符合设计意图
- 是否有安全漏洞（注入、密钥泄露、权限绕过）
- 是否存在性能问题或资源泄漏
- 测试覆盖是否充分
- 代码可读性和可维护性
- 是否遵循项目现行风格

Contributor 收到 Review 意见后应在合理时间内回应或修改。长时间无响应的 PR 可能被关闭。
