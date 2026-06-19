# Agent Review CI — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新建一个 GitHub Actions workflow，在 PR 涉及核心源码时自动触发 Anthropic Claude Code 审查，以 PR Review 形式提交结果，自动打标签，status check 阻挡 merge。

**Architecture:** 单文件 `.github/workflows/agent-review.yml`，1 个 job 3 个 step：review、打标签、fail check。与现有 `ci.yml` 并行，互不影响。

**Tech Stack:** GitHub Actions, anthropics/claude-code-action@v1, gh CLI

---

## 文件职责说明

| 文件 | 改动内容 |
|------|----------|
| `.github/workflows/agent-review.yml` | 新建 — 完整 workflow 文件 |

---

### Task 1: 创建 agent-review.yml

**Files:**
- Create: `.github/workflows/agent-review.yml`

- [ ] **Step 1: 创建 workflow 文件**

```yaml
name: Agent Review

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

jobs:
  agent-review:
    runs-on: ubuntu-24.04
    timeout-minutes: 15
    permissions:
      contents: read
      pull-requests: write
    steps:
      - uses: actions/checkout@v4

      - id: review
        continue-on-error: true
        uses: anthropics/claude-code-action@v1
        env:
          ANTHROPIC_API_KEY: ${{ secrets.ANTHROPIC_API_KEY }}
        with:
          prompt: |
            You are reviewing a pull request. Review the diff for:
            - Memory safety (use-after-free, double-delete, dangling references)
            - Concurrency safety (data races, lock ordering)
            - Exception safety (RAII, resource cleanup)
            - Architecture consistency (DI patterns, file boundaries)
            - Security vulnerabilities (SSRF, command injection, XSS)

            Post your review as a PR review. If you find CRITICAL or IMPORTANT issues,
            use "request changes". If only minor issues or none, "approve".

            Use Chinese for the review body (this is a Chinese project).
          claude_args: "--max-turns 5"

      - name: Label ai-reviewed
        if: steps.review.outcome == 'success'
        run: gh pr edit --add-label "ai-reviewed"
        env:
          GH_TOKEN: ${{ github.token }}

      - name: Label needs-changes
        if: steps.review.outcome == 'failure'
        run: gh pr edit --add-label "needs-changes"
        env:
          GH_TOKEN: ${{ github.token }}

      - name: Block merge if needs changes
        if: steps.review.outcome == 'failure'
        run: |
          echo "Agent review found issues — merge blocked"
          exit 1
```

- [ ] **Step 2: 验证 YAML 语法**

```bash
cd /home/icepop/Merak && python3 -c "import yaml; yaml.safe_load(open('.github/workflows/agent-review.yml'))" && echo "YAML valid"
```
Expected: `YAML valid`

- [ ] **Step 3: 提交**

```bash
cd /home/icepop/Merak && git add -f .github/workflows/agent-review.yml && git commit -m "ci: add Agent Review workflow for core source PRs

Automatically triggers anthropics/claude-code-action on PRs touching
libs/{core,loop,app,worldbuilding,tools,runtime}/** or webui/**.
Posts PR review, adds ai-reviewed/needs-changes label, and blocks
merge via status check when issues are found."
```

---

## Post-Deploy

PR merge 后需要仓库管理员做一次配置：

1. 在 GitHub repo Settings → Secrets → Actions 中添加 `ANTHROPIC_API_KEY`
2. 在 Settings → Branches → Branch protection rules → `main` 中勾选 `Agent Review` 作为 required status check
3. 手动创建 `ai-reviewed` 和 `needs-changes` 两个标签（或在 PR 中首次触发后自动出现）
