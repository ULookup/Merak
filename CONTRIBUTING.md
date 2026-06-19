# Contributing to Merak / Merak 贡献指南

感谢你对 Merak 的关注！本文档帮助你了解如何参与贡献。

Thanks for your interest in Merak! This document helps you understand how to contribute.

---

## 行为准则 / Code of Conduct

本项目采用 [Contributor Covenant 2.1](CODE_OF_CONDUCT.md)。参与即视为同意遵守。

This project adopts the [Contributor Covenant 2.1](CODE_OF_CONDUCT.md). Participation constitutes agreement to abide by it.

## 如何报告问题 / Reporting Bugs

1. 先在 [Issues](../../issues) 中搜索是否已有相同问题 / Search existing issues first
2. 使用 Bug Report 模板提交，包含 / Use the Bug Report template, include:
   - Merak 版本号 / Version
   - 操作系统和版本 / OS and version
   - 复现步骤 / Steps to reproduce
   - 期望行为 vs 实际行为 / Expected vs actual behavior
   - 相关日志或截图 / Relevant logs or screenshots

安全问题请勿公开提交 Issue，通过私密渠道联系维护者。

For security issues, do NOT file a public issue. Contact maintainers privately.

## 如何提出新功能 / Feature Requests

1. 先在 Issues 中搜索是否已有类似提议 / Search existing proposals first
2. 使用 Feature Request 模板，说明 / Use the Feature Request template, describe:
   - 使用场景 / Use case
   - 期望的解决方案 / Proposed solution
   - 备选方案（如果有）/ Alternatives considered

## 开发环境搭建 / Development Setup

### 前置要求 / Prerequisites

| 工具 Tool | 最低版本 Minimum | 用途 Purpose |
|---|---|---|
| CMake | 3.28 | C++ build |
| Conan | 2.x | C++ dependency management |
| GCC / Clang / MSVC | C++23 support | Compiler |
| Node.js | 22 | WebUI / Desktop |
| Rust | 1.80+ | Tauri Desktop |
| PostgreSQL | 16 | Data storage (optional) |

### C++ Backend

```bash
# Install dependencies / 安装依赖
conan install . --build=missing -s build_type=Debug

# Configure / 配置
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug

# Build / 编译
cmake --build build -j

# Run / 运行
./build/cli/Debug/merak serve --port 3888
```

### WebUI (React 19)

```bash
cd webui
npm install
npm run dev       # Development / 开发模式
npm run lint      # Lint / 代码检查
npm run test      # Run tests / 运行测试
npm run build     # Production build / 生产构建
```

### Desktop (Tauri 2)

```bash
npm install
npm --prefix webui install
npm --prefix apps/desktop install
npm run desktop:dev    # Development / 开发模式
npm run desktop:check  # Type check / 类型检查
npm run desktop:build  # Package / 打包
```

### Running Tests / 运行测试

```bash
# C++ tests
cd build && ctest --output-on-failure

# WebUI tests
cd webui && npm run test
```

## 开发流程 / Development Workflow

1. Fork 本仓库 / Fork the repository
2. Clone 你的 Fork / Clone your fork
3. 创建功能分支 / Create a feature branch (see [Branch Strategy](#分支策略--branch-strategy))
4. 开发并自测 / Develop and self-test
5. 确保 CI 通过 / Ensure CI passes
6. 提交 PR / Submit a PR

## Commit 规范 / Commit Conventions

采用 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/v1.0.0/)：

```
<type>(<scope>): <subject>

<body>
```

### Type

| Type | 说明 Description |
|---|---|
| `feat` | 新功能 New feature |
| `fix` | Bug 修复 Bug fix |
| `docs` | 文档更新 Documentation |
| `style` | 代码格式 Formatting (no logic change) |
| `refactor` | 重构 Refactoring |
| `perf` | 性能优化 Performance |
| `test` | 测试相关 Testing |
| `chore` | 构建/工具/依赖 Build/tooling/deps |
| `ci` | CI 配置 CI configuration |

### Scope

```
worldbuilding  worldbuilding engine / 世界观引擎
runtime        会话、运行、事件 / sessions, runs, events
http           REST API / SSE
llm            LLM Provider
memory         记忆系统 / memory system
tools          工具系统 / tool system
webui          前端工作台 / frontend workbench
desktop        Tauri 桌面端 / Tauri desktop
config         配置系统 / configuration
kg             知识图谱 / knowledge graph
skills         技能系统 / skill system
cli            CLI 入口 / CLI entrypoint
```

### 示例 / Example

```
feat(worldbuilding): add timeline event recording endpoint

POST /api/worldbuilding/:wid/timeline for manual timeline event recording.
POST /api/worldbuilding/:wid/timeline 用于手动记录时间线事件。
```

## 分支策略 / Branch Strategy

- `main` — 稳定分支，随时可发布 / Stable, ready to release
- `feat/<name>` — 新功能 / New feature
- `fix/<name>` — Bug 修复 / Bug fix
- `docs/<name>` — 文档 / Documentation

始终基于 `main` 最新提交创建分支 / Always branch from the latest `main`.

## 代码风格 / Code Style

### C++

- C++23 standard
- Follow the existing code structure / 遵循项目现有代码结构
- Use `#pragma once` for headers
- Namespace: `merak` and sub-namespaces

### TypeScript / React

- Strict mode / 严格模式
- Prefer function components + Hooks / 优先使用函数组件 + Hooks
- Use CSS Modules for styling / CSS Modules 管理样式
- Run `npm run lint` to validate / 通过 `npm run lint` 检查

### Rust (Tauri)

- Follow `cargo clippy` suggestions
- Keep Tauri commands lightweight / 保持 Tauri command 轻量

## 测试规范 / Testing

- Bug fixes must include regression tests / Bug 修复必须补充回归测试
- New features should have unit or integration tests / 新功能应有对应测试
- C++ tests live in `tests/` and `libs/*/tests/`
- WebUI tests live in `webui/src/__tests__/`

## Pull Request 流程 / Pull Request Process

1. 确保分支与 `main` 同步 / Rebase on `main` (`git rebase main`)
2. Your PR should / 你的 PR 应该：
   - Pass all CI checks / 通过所有 CI 检查
   - Include screenshots for UI changes / UI 变更附带截图
   - Follow the PR template / 参考 PR 模板填写说明
3. 至少一位维护者 Review 通过后方可合并 / At least one maintainer approval required
4. 合并使用 Squash Merge / Merged via Squash Merge

## Code Review 标准 / Code Review Standards

Reviewer 会关注 / Reviewers will check:

- 功能是否符合设计意图 / Does it match the intended design?
- 是否有安全漏洞 / Security: injection, credential leaks, privilege escalation
- 是否存在性能问题或资源泄漏 / Performance issues or resource leaks
- 测试覆盖是否充分 / Adequate test coverage
- 代码可读性和可维护性 / Readability and maintainability
- 是否遵循项目现行风格 / Consistency with project conventions

Contributor 收到 Review 意见后应在合理时间内回应或修改。长时间无响应的 PR 可能被关闭。

Contributors should respond to review feedback within a reasonable timeframe. Stale PRs may be closed.
