# Contributing to Merak

## Code of Conduct

This project adopts the [Contributor Covenant 2.1](CODE_OF_CONDUCT.md). Participation constitutes agreement to abide by it.

## Reporting Bugs

1. Search [Issues](../../issues) first
2. Use the Bug Report template, include: version, OS, steps to reproduce, expected vs actual behavior, relevant logs or screenshots

For security issues, do NOT file a public issue. Contact maintainers privately.

## Feature Requests

1. Search existing proposals first
2. Use the Feature Request template, describe: use case, proposed solution, alternatives considered

## Development Setup

### Prerequisites

| Tool | Minimum | Purpose |
|---|---|---|
| CMake | 3.28 | C++ build |
| Conan | 2.x | C++ dependency management |
| GCC / Clang / MSVC | C++23 support | Compiler |
| Node.js | 22 | WebUI / Desktop |
| Rust | 1.80+ | Tauri Desktop |
| PostgreSQL | 16 | Data storage (optional) |

### C++ Backend

```bash
conan install . --build=missing -s build_type=Debug
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/cli/Debug/merak serve --port 3888
```

### WebUI (React 19)

```bash
cd webui
npm install
npm run dev       # development
npm run lint      # lint
npm run test      # run tests
npm run build     # production build
```

### Desktop (Tauri 2)

```bash
npm install
npm --prefix webui install
npm --prefix apps/desktop install
npm run desktop:dev    # development
npm run desktop:check  # type check
npm run desktop:build  # package
```

### Running Tests

```bash
# C++ tests
cd build && ctest --output-on-failure

# WebUI tests
cd webui && npm run test
```

## Development Workflow

1. Fork the repository
2. Clone your fork
3. Create a feature branch (see [Branch Strategy](#branch-strategy))
4. Develop and self-test
5. Ensure CI passes
6. Submit a PR

## Commit Conventions

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <subject>

<body>
```

| Type | Description |
|---|---|
| `feat` | New feature |
| `fix` | Bug fix |
| `docs` | Documentation |
| `style` | Formatting (no logic change) |
| `refactor` | Refactoring |
| `perf` | Performance |
| `test` | Testing |
| `chore` | Build/tooling/deps |
| `ci` | CI configuration |

Scope: `worldbuilding`, `runtime`, `http`, `llm`, `memory`, `tools`, `webui`, `desktop`, `config`, `kg`, `skills`, `cli`

Example:

```
feat(worldbuilding): add timeline event recording endpoint
```

## Branch Strategy

- `main` — stable, ready to release
- `feat/<name>` — new feature
- `fix/<name>` — bug fix
- `docs/<name>` — documentation

Always branch from the latest `main`.

## Code Style

### C++

- C++23 standard, `#pragma once`, namespace `merak` and sub-namespaces

### TypeScript / React

- Strict mode, function components + Hooks, CSS Modules, `npm run lint`

### Rust (Tauri)

- `cargo clippy`, keep Tauri commands lightweight

## Testing

- Bug fixes must include regression tests
- New features should have unit or integration tests
- C++: `tests/` and `libs/*/tests/`
- WebUI: `webui/src/__tests__/`

## Pull Request Process

1. Rebase on `main`
2. Your PR should: pass CI, include screenshots for UI changes, follow the PR template
3. At least one maintainer approval required
4. Merged via Squash Merge

## Code Review

Reviewers check: design intent, security, performance, test coverage, readability, consistency.

Contributors should respond to review feedback within a reasonable timeframe. Stale PRs may be closed.

---

# Merak 贡献指南

## 行为准则

本项目采用 [Contributor Covenant 2.1](CODE_OF_CONDUCT.md)。参与即视为同意遵守。

## 报告 Bug

1. 先在 [Issues](../../issues) 中搜索是否已有相同问题
2. 使用 Bug Report 模板提交，包含：版本、操作系统、复现步骤、期望行为 vs 实际行为、相关日志或截图

安全问题请勿公开提交 Issue，通过私密渠道联系维护者。

## 功能请求

1. 先在 Issues 中搜索是否已有类似提议
2. 使用 Feature Request 模板，说明：使用场景、期望的解决方案、备选方案

## 开发环境

### 前置要求

| 工具 | 最低版本 | 用途 |
|---|---|---|
| CMake | 3.28 | C++ 构建 |
| Conan | 2.x | C++ 依赖管理 |
| GCC / Clang / MSVC | C++23 | 编译器 |
| Node.js | 22 | WebUI / Desktop |
| Rust | 1.80+ | Tauri Desktop |
| PostgreSQL | 16 | 数据存储（可选） |

### C++ 后端

```bash
conan install . --build=missing -s build_type=Debug
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
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
3. 创建功能分支（见[分支策略](#分支策略-1)）
4. 开发并自测
5. 确保 CI 通过
6. 提交 PR

## Commit 规范

采用 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/v1.0.0/)：

```
<type>(<scope>): <subject>

<body>
```

| Type | 说明 |
|---|---|
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `docs` | 文档 |
| `style` | 代码格式 |
| `refactor` | 重构 |
| `perf` | 性能优化 |
| `test` | 测试 |
| `chore` | 构建/工具/依赖 |
| `ci` | CI 配置 |

Scope：`worldbuilding`、`runtime`、`http`、`llm`、`memory`、`tools`、`webui`、`desktop`、`config`、`kg`、`skills`、`cli`

示例：

```
feat(worldbuilding): add timeline event recording endpoint
```

## 分支策略

- `main` — 稳定分支，随时可发布
- `feat/<name>` — 新功能
- `fix/<name>` — Bug 修复
- `docs/<name>` — 文档

始终基于 `main` 最新提交创建分支。

## 代码风格

### C++

- C++23 标准，`#pragma once`，命名空间 `merak` 及子命名空间

### TypeScript / React

- 严格模式，函数组件 + Hooks，CSS Modules，`npm run lint`

### Rust (Tauri)

- `cargo clippy`，保持 Tauri command 轻量

## 测试规范

- Bug 修复必须补充回归测试
- 新功能应有对应的单元测试或集成测试
- C++：`tests/` 和 `libs/*/tests/`
- WebUI：`webui/src/__tests__/`

## Pull Request 流程

1. 确保分支与 `main` 同步
2. 你的 PR 应：通过 CI、UI 变更附带截图、参考 PR 模板
3. 至少一位维护者 Review 通过后方可合并
4. 合并使用 Squash Merge

## Code Review

Review 关注：设计意图匹配、安全漏洞、性能、测试覆盖、可读性、一致性。

收到 Review 意见后应在合理时间内回应。长时间无响应的 PR 可能被关闭。
