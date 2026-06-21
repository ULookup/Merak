# PR Agent Review CI — 设计文档

> **For agentic workers:** 实现计划编写时需要将此 spec 转化为具体任务。

**Goal:** 在涉及核心源码改动的 PR 中，自动触发 Anthropic Claude 官方 Action 进行代码审查，以 PR Review 形式贴评论，同时创建 status check block merge，根据结果自动打标签。

**Architecture:** 新增一个独立 GitHub Actions workflow（`agent-review.yml`），与现有 `ci.yml` 并行运行。使用 `anthropics/claude-code-action` 官方 Action 进行审查，通过 `gh` CLI 操作 PR review 和标签。

**Tech Stack:** GitHub Actions, anthropics/claude-code-action, gh CLI

---

## 触发条件

```yaml
on:
  pull_request:
    paths:
      - 'libs/core/**'
      - 'libs/loop/**'
      - 'libs/app/**'
      - 'libs/worldbuilding/**'
      - 'libs/tools/**'
      - 'libs/runtime/**'
      - 'webui/**'
```

只在上述路径有文件改动时触发，避免 docs/config 等修改浪费审阅资源。

## Review 流程

```
PR push → paths match → agent-review job 启动
         → anthropics/claude-code-action 审查 diff
         → 通过 → gh pr review approve
                → gh pr edit --add-label "ai-reviewed"
         → 不通过 → gh pr review request-changes
                  → gh pr edit --add-label "needs-changes"
```

## Job 规格

- **Runs-on:** ubuntu-24.04（与现有 CI 一致）
- **Timeout:** 15 minutes（LLM 调用时间不可控，给足余量）
- **Permissions:** `pull-requests: write`（写 review）+ `contents: read`（读代码）

## 标签

| 标签 | 含义 |
|------|------|
| `ai-reviewed` | Agent review 通过，无 Critical/Important 问题 |
| `needs-changes` | 存在 Critical 或 Important 级别问题，需修复后重新触发 |

## Status Check

Job 名称 `Agent Review`，失败时阻挡 merge。需要仓库开启 branch protection，勾选此 check 作为 required。

## 审查标准

Agent 审查时关注：
- 内存安全（use-after-free, double-delete, dangling ref）
- 并发安全（data race, lock ordering）
- 异常安全（RAII, resource cleanup）
- 架构一致（是否遵循现有 DI 模式、文件边界）
- 安全漏洞（SSRF, command injection, XSS）

## 与现有 CI 的关系

- 不改动 `ci.yml`
- 与 `build-cpp` / `build-webui` 并行执行
- 两者独立——build 失败不跳过 review，review 失败不跳过 build
