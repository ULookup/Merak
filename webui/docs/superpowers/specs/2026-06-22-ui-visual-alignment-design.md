# UI Visual Alignment Design

## Summary

将 WebUI 的视觉实现与 DESIGN.md 设计规范对齐。当前存在三套配色系统并存的问题：DESIGN.md 定义暖灰单色品牌，global.css 使用 Tailwind Slate/Blue 色板，DesktopShell 硬编码独立蓝色调。此方案以 DESIGN.md 为唯一真相源，统一颜色系统、消除硬编码、规范化动效 Token。

## Scope

4 个 Layer，按顺序交付：

### Layer 1: 全局 CSS 变量重写

**文件:** `src/styles/global.css` (:root 块)

将所有 Color Token 从 Tailwind Slate 色板替换为 DESIGN.md 的暖灰单色系统：

| 变量 | 当前值 | 目标值 (DESIGN.md) |
|------|--------|-------------------|
| --ink | #0f172a | #171717 |
| --ink-soft | #1e293b | #333333 |
| --page | #ffffff | #f7f7f5 |
| --page-warm | #f1f5f9 | #eeeeeb |
| --surface-muted | #f1f5f9 | #f3f3f1 |
| --surface-strong | #f8fafc | #fafafa |
| --border | #e2e8f0 | #e0dfdc |
| --border-strong | #cbd5e1 | #c8c7c3 |
| --muted | #64748b | #77756f |
| --muted-strong | #475569 | #55524c |
| --brand | #06266f | #343434 |
| --brand-soft | #dbe7ff | #e8e7e4 |
| --brand-strong | #041c55 | #55524c |

**新增变量:**
- `--border-subtle: rgba(224, 223, 220, 0.92)` — 柔和卡片边框，用于与 --border (solid) 互补

**移除的冗余变量:**
- `--teal, --teal-strong, --teal-soft, --teal-wash` — 语义混乱，统一为 --ink / --surface-muted / --page
- `--indigo, --violet, --purple` — 三个同义变量，统一为 --muted-strong
- `--hover: rgba(6, 38, 111, 0.06)` — 蓝色残留，改为 `rgba(23, 23, 23, 0.06)`
- `--brand-wash` — 非 DESIGN.md 变量，引用替换为 --surface-muted

**语义色:** amber (#8a5a12)、ruby (#be123c)、green (#16a34a) 已与 DESIGN.md 对齐，保持不变。

**新建动效 Token (global.css 中新增):**
- `--ease-out: cubic-bezier(0.4, 0, 0.2, 1)` — 退出/消失动画
- `--anim-lift-sm: translateY(-1px)` — hover 微升
- `--anim-lift-md: translateY(-3px)` — hover 明显升
- `--anim-press: scale(0.98)` — 点击下沉
- `--anim-fade-in: 200ms var(--ease) both` — 页面切换/列表入场淡入

**Type Token 更新 (对齐 DESIGN.md typography):**
- body fontSize: 13px (已一致)
- title fontSize: 16px, fontWeight: 780 (需检查)
- label fontSize: 10px, fontWeight: 720, letterSpacing: 0.04em, uppercase (需统一)

### Layer 2: DesktopShell 视觉统一

**文件:** `src/shell/DesktopShell.module.css`

将所有硬编码颜色替换为 CSS 变量引用：

| 硬编码值 | 替换为 |
|----------|--------|
| #fff (sidebar, worldCard, search) | var(--surface) |
| #e2e8f0 (border) | var(--border) |
| #dce3ed | rgba(224,223,220,0.92) |
| #17213b | var(--ink) |
| #263653 | var(--muted-strong) |
| #667085 | var(--muted) |
| #f8fafc | var(--surface-muted) |
| #f1f5fb (hover) | var(--surface) |
| #06266f / --shell-navy | var(--brand) |
| rgb(15 23 42 / 38%) (backdrop) | rgba(23,23,23,0.38) |

**视觉特性对齐:**
- Sidebar: `backdrop-filter: blur(18px)` + 半透明背景 (与 WorldSidebar 一致)
- 导航激活态: 蓝色 solid → `linear-gradient(90deg, var(--brand-soft), var(--surface))` + inset left rail
- 顶栏: `backdrop-filter: blur(14px)` + 半透明背景 (与 MainPanel header 一致)
- 保留现有布局结构，只改风格层

### Layer 3: 组件颜色审计

**批量替换 (全局搜索替换):**
- 所有 `rgba(224, 223, 220, 0.92)` → `var(--border-subtle)` (约 30 处)

**页面级 --brand 替换:**
- `OverviewPage.module.css`: 所有 `var(--brand)` 文字色 → `var(--ink)`，brand 背景 → `var(--brand-soft)` 或 `var(--surface-muted)`
- `ChaptersPage.module.css`: 进度条 track 色 → `var(--muted-strong)`
- `ScenesPage.module.css`: 创建按钮 → 接入主按钮样式
- `SecretsPage.module.css`: 同上
- `FilesPage.module.css`: 同上

**PipelineNavigator 硬编码修复:**
- 错误色 #c00 / background #fff0f0 → `var(--ruby)` / `var(--ruby-soft)`
- 完成色 #2e7d32 / background #f0fff0 → `var(--green)` / `var(--green-soft)`
- 确认按钮 #4a90d9 → `var(--ink)`
- 对话框背景 #fff → `var(--surface)`

**AgentCard / AgentStatusBar:**
- 独立颜色系统 → 使用 DESIGN.md 语义色 + 中性色变量

**ChapterEditor:**
- 编辑器面板色调 → 对齐 InspectorPanel 的 editor panel 样式

### Layer 4: 交互反馈补充

**新增过渡动画:**
1. **页面切换:** pageContent 加 200ms opacity + translateY(6px) fade-in
2. **Agent 切换:** ChatTimeline 内容 crossfade (200ms)，Agent 名称 slide-in
3. **编辑器退出:** EditorOverlay 加 pageOut 动画 (280ms, reverse spring)
4. **列表操作:** 新增项 messageIn 入场，删除项 shrink-out (200ms)

**Run 状态流可视化:**
- PipelineNavigator phase 切换加 height morph + 颜色渐变过渡
- RunInspector 加 indeterminate 进度指示器

**文件保存状态:**
- saving 状态: 微 spinner
- saved 状态: checkmark 弹入 → 1200ms 淡出

**Inspector Tab 切换:**
- 内容区 180ms crossfade (opacity 过渡)

**影响文件:** App.tsx, MainPanel.tsx, InspectorPanel.tsx, DesktopShell.tsx, ChapterEditor.tsx, PipelineNavigator.tsx, RunInspector.tsx

### Out of Scope

- 不改变任何组件的 DOM 结构或 props 接口
- 不改变路由逻辑
- 不改变状态管理 (AppState)
- 不新增第三方动画库
- 不改动后端/API
- 不改变 Tauri 集成

## Component Breakdown

### 受影响文件清单

**CSS 变量 (1 file):**
- `src/styles/global.css` — :root 块重写

**Shell (1 file):**
- `src/shell/DesktopShell.module.css` — 全量颜色替换 + glass 语言

**Components (15 files):**
- `src/components/InspectorPanel.module.css` — border-subtle 替换, tab 过渡
- `src/components/MainPanel.module.css` — border-subtle 替换
- `src/components/Composer.module.css` — border-subtle 替换
- `src/components/ChatTimeline.module.css` — border-subtle 替换, crossfade
- `src/components/WorldSidebar.module.css` — border-subtle 替换
- `src/components/WorldDashboard.module.css` — border-subtle 替换
- `src/components/WorldOnboarding.module.css` — brand-wash 替换
- `src/components/ChapterEditor.module.css` — 对齐 editor panel 样式
- `src/components/Sidebar.module.css` — 变量引用检查
- `src/components/Sidebar/PipelineNavigator.module.css` — 硬编码修复
- `src/components/Inspector/AgentCardEdit.module.css` — 变量引用检查
- `src/components/Inspector/AgentCardView.module.css` — 变量引用检查
- `src/components/Inspector/AgentPromptViewer.module.css` — 变量引用检查
- `src/components/Inspector/CreationDashboard.module.css` — 变量引用检查
- `src/components/cells/Cells.module.css` — border-subtle 替换

**Pages (9 files):**
- `src/pages/OverviewPage.module.css` — --brand → --ink
- `src/pages/ChaptersPage.module.css` — --brand 替换
- `src/pages/ScenesPage.module.css` — --brand 替换
- `src/pages/SecretsPage.module.css` — --brand 替换
- `src/pages/FilesPage.module.css` — --brand 替换
- `src/pages/CharactersPage.module.css` — 变量引用检查
- `src/pages/ForeshadowingPage.module.css` — 变量引用检查
- `src/pages/WorldPage.module.css` — 变量引用检查
- `src/pages/SessionsPage.module.css` — 变量引用检查

**TSX 逻辑修改 (5 files):**
- `src/shell/DesktopShell.tsx` — Shell 侧边栏可接入 BrandMark (可选)
- `src/App.tsx` — 页面切换动画逻辑
- `src/components/MainPanel.tsx` — Agent 切换过渡逻辑
- `src/components/InspectorPanel.tsx` — Tab 切换 crossfade
- `src/components/ChapterEditor.tsx` — 退出动画逻辑

## Testing Strategy

**视觉回归测试:**
- 在亮色主题下逐页截图对比（Overview → Workbench → Editor → Settings）
- 检查所有 hover/press/focus 状态
- 验证移动端响应式断点

**组件测试:**
- 已有 vitest 测试套件，不需新增测试文件
- 确保 CSS 变量引用在 JSDOM 环境下不报错

**手动检查:**
- 所有使用 `--brand` 的页面确认颜色语义正确
- DesktopShell 导航激活/悬停状态
- WorldSidebar agent list 激活态
- Composer 发送按钮/取消按钮 hover
- PipelineNavigator error/complete banner

## Delivery

分 4 个 Commit 按 Layer 顺序交付：

1. `style: align global CSS variables with DESIGN.md` — Layer 1
2. `style: unify DesktopShell with workbench glass language` — Layer 2
3. `style: replace hardcoded colors with CSS variables across components` — Layer 3
4. `feat: add interaction transitions and animation tokens` — Layer 4
