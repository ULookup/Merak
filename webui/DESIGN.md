---
name: Merak WebUI
description: 本地优先的 AI 创作工作台——为长篇小说和世界观构建者打造的 Agent 工作界面
colors:
  ink: "#171717"
  ink-soft: "#333333"
  page: "#f7f7f5"
  page-warm: "#eeeeeb"
  surface: "#ffffff"
  surface-muted: "#f3f3f1"
  surface-strong: "#fafafa"
  muted: "#77756f"
  muted-strong: "#55524c"
  border: "#e0dfdc"
  border-strong: "#c8c7c3"
  brand: "#343434"
  brand-soft: "#e8e7e4"
  amber: "#8a5a12"
  amber-soft: "#fbf2dc"
  ruby: "#be123c"
  ruby-soft: "#ffe4e6"
  green: "#16a34a"
  green-soft: "#dcfce7"
typography:
  body:
    fontFamily: "Inter, ui-sans-serif, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif"
    fontSize: "13px"
    fontWeight: 400
    lineHeight: 1.55
  title:
    fontFamily: "Inter, ui-sans-serif, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif"
    fontSize: "16px"
    fontWeight: 780
    lineHeight: 1.2
  label:
    fontFamily: "Inter, ui-sans-serif, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif"
    fontSize: "10px"
    fontWeight: 720
    lineHeight: 1.35
    letterSpacing: "0.04em"
    textTransform: "uppercase"
  mono:
    fontFamily: "'SF Mono', Monaco, Consolas, 'Liberation Mono', monospace"
    fontSize: "12px"
    fontWeight: 400
    lineHeight: 1.55
rounded:
  sm: "4px"
  md: "6px"
  lg: "8px"
  xl: "12px"
  full: "999px"
spacing:
  xs: "4px"
  sm: "8px"
  md: "12px"
  lg: "16px"
  xl: "24px"
components:
  button-primary:
    backgroundColor: "{colors.ink}"
    textColor: "#ffffff"
    rounded: "{rounded.md}"
    padding: "10px 14px"
  button-primary-hover:
    backgroundColor: "#303030"
  button-secondary:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.muted-strong}"
    rounded: "{rounded.md}"
    padding: "9px 13px"
  button-ghost:
    backgroundColor: "transparent"
    textColor: "{colors.muted-strong}"
    rounded: "{rounded.lg}"
    padding: "7px 10px"
  input:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    rounded: "{rounded.md}"
    padding: "9px 11px"
  card:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    rounded: "{rounded.md}"
    padding: "12px"
  chip:
    backgroundColor: "{colors.surface-muted}"
    textColor: "{colors.muted-strong}"
    rounded: "{rounded.md}"
    padding: "5px 8px"
---

# Design System: Merak WebUI

## 1. Overview

**Creative North Star: "The Typeshop"**

Merak WebUI 是一个铸字车间——精确、利落、每一像素都有存在的理由。它不是装饰性的设计工作室，而是一个生产文字的工厂，界面是服务于这个目的的工具。就像 Codex 编辑器的极简布局承载了代码一样，Merak 的界面承载的是叙事。

系统的美学哲学根植于单色品牌（mono-brand）：近黑就是品牌色。没有花哨的渐变强调色，没有霓虹点缀。层次来自亮度的微妙递进——page、surface、muted、ink 之间的明暗差异构成了全部的视觉深度。这是一个"安静的自信"系统：Premium 感不来自堆叠装饰，而来自精确的间距、考究的字体粗细和克制的交互反馈。

组件触感是精确利落的（Tactile & Crisp）：悬停微升（`translateY(-1px)`），点击下沉（`scale(0.98)`），就像按下真实的物理按钮。这种物理性贯穿所有交互元素。

**Key Characteristics:**
- 单色品牌系统：near-black 就是强调色
- 精确利落的触感交互：lift on hover, sink on press
- 玻璃侧边栏：唯一使用 backdrop-filter 的表面，作为空间层次信号
- Inter 单一字体族，通过粗细（650–900）构建层级
- 克制阴影：只有三层（sm/md/lg），flat-by-default，hover 时微升
- 动效可以华丽：ease-spring 曲线、弹性入场动画、pulse 呼吸效果

系统明确拒绝：游戏/主播风格（霓虹、高饱和）、SaaS 营销模板（hero、渐变 CTA）、传统企业软件（密集表格、无留白）。

## 2. Colors

单色品牌系统。Ink（#171717）是唯一的强调色——它同时承担文本色、主按钮背景、品牌标识色三重角色。颜色层次完全通过亮度递进来构建，从 near-black 到 warm off-white。

### Primary
- **Ink** (#171717): 系统唯一的强调色。同时用于正文、主按钮背景、品牌标识、焦点环。在亮色背景下承担最高的对比度负载。

### Neutral
- **Page** (#f7f7f5): 根背景色。微暖的 off-white，在白色屏幕上提供最低限度的暖意而不显黄。
- **Page Warm** (#eeeeeb): 比 page 稍暖稍暗，用于 tab 栏背景等需要区分的区域。
- **Surface** (#ffffff): 纯白卡片/面板背景。所有内容容器的默认色。
- **Surface Muted** (#f3f3f1): 低一档的容器背景，用于需要与 surface 区分的内层区块。
- **Surface Strong** (#fafafa): 介于 surface 和 surface-muted 之间，用于输入框背景。
- **Muted** (#77756f): 三级文本和占位符。≥4.5:1 对白底，满足 WCAG AA 正文对比度。
- **Muted Strong** (#55524c): 二级文本，标签，辅助信息。比 muted 深一档。
- **Border** (#e0dfdc): 默认边框和分割线。
- **Border Strong** (#c8c7c3): 悬停态边框，更强的分割需求。

### Accent (语义色)
- **Amber** (#8a5a12) / **Amber Soft** (#fbf2dc): 警告、审批等待、时间提醒。不常用，仅在有明确语义需求时出现。
- **Ruby** (#be123c) / **Ruby Soft** (#ffe4e6): 错误、取消、危险操作。始终配软背景使用，不单独出现。
- **Green** (#16a34a) / **Green Soft** (#dcfce7): 成功状态、工具完成指示。

### Named Rules

**The Mono-Brand Rule.** 系统的强调色就是 near-black。没有第二个品牌色。没有渐变。Ink 在任一屏幕上占用的面积不超过 10%，它的稀缺性本身就是信号。

**The One Accent Per Context Rule.** 语义色（amber / ruby / green）每次只出现一种。不同时使用两种语义色争夺注意力。

**The Gray-Is-Not-Elegance Rule.** 灰字在非白背景上会显得脏。`muted` (#77756f) 只在纯白 surface 上使用。在 tinted 背景上，用更深一档的 `muted-strong` 或透明度叠印。

## 3. Typography

**Primary Font:** Inter (with system sans-serif fallback)
**Mono Font:** SF Mono, Monaco, Consolas, Liberation Mono

**Character:** Inter 是系统唯一的字体族。不引入第二个字体族来制造"文学感"——那会稀释 Typeshop 的精确气质。层级完全通过粗细（650–900）和尺寸（10px–18px）来构建，不依赖字体切换。

### Hierarchy
- **Display** (780, 18px, 1.2): 世界名称、大标题。只在 Inspector hero block 中使用。
- **Title** (780, 16px, 1.2): 面板标题、Inspector 区块标题。
- **Subtitle** (760–780, 14px, 1.2): 侧边栏导航项、主面板标题。
- **Body** (400, 13px, 1.55): 正文。最大行宽 75ch（约 820px），超出自动折行。
- **Small Body** (400, 12px, 1.5–1.6): 辅助内容、卡片描述、文件路径。
- **Caption** (650–700, 11px, 1.35–1.45): 元数据、时间戳、文件大小。
- **Label** (720–800, 10px, 0.04em, uppercase): 区块标签、kicker。仅在 Inspector 区块标题上方和字段标签中使用，不在每个 section 上方添加。

### Named Rules

**The Single-Font Rule.** 只用 Inter 一个字体族（加等宽 Mono 用于代码）。不引入衬线体。层级靠粗细和尺寸。

**The No-Eyebrow-Army Rule.** Label（10px uppercase）只用于表单字段和 Inspector 区块标题。不在每个 section 上方添加一个独立的小号大写 eyebrow——那是 AI 模板的默认肌肉记忆。

## 4. Elevation

系统以 flat-by-default 为原则。卡片和面板初始不带阴影，通过 1px 边框与背景分离。阴影只在两种情况下出现：(1) sidebar 作为全局导航层需要 lift 感；(2) 悬停态作为反馈信号。

### Shadow Vocabulary
- **Shadow Sm** (`0 1px 2px rgba(23, 23, 23, 0.06)`): 工作区面板、空状态卡片。最低限度的 lift，几乎不可见，用于暗示可交互表面。
- **Shadow Md** (`0 12px 28px rgba(23, 23, 23, 0.1)`): 侧边栏、toast、模态框。明显的浮起感，用于全局或跨层元素。
- **Shadow Lg** (`0 18px 46px rgba(23, 23, 23, 0.12)`): 保留，当前未大量使用。
- **Ring** (`0 0 0 3px rgba(23, 23, 23, 0.12)`): 焦点环，统一用于所有可聚焦元素。

### Named Rules

**The Flat-By-Default Rule.** 表面在静止状态下是平的。阴影作为状态响应出现（hover lift、全局叠加），不作为静态装饰。唯一例外是 sidebar——它承担全局导航层的 z-index，需要 lift 来传达空间层级。

**The One-Glass-Surface Rule.** `backdrop-filter: blur()` 只用于侧边栏。不扩散到卡片、模态框、或任何其他表面。玻璃材质是导航层的专属语言。

## 5. Components

### Buttons

**Shape:** 6–8px 圆角，依尺寸而定。大按钮 8px，小按钮/工具栏按钮 6px。

**Primary (Solid Ink):** `background: var(--ink)`, `color: #fff`, `padding: 10px 14px`, `border-radius: 6–8px`, `font-weight: 720`。Hover: 背景微亮至 `#303030`，`translateY(-1px)`。Active: `translateY(0) scale(0.98)`。Disabled: opacity 0.4，光标 default。用于发送消息、确认创建等主要操作。

**Secondary (Bordered):** `background: var(--surface-strong)`, `border: 1px solid var(--border)`, `color: var(--muted-strong)`。Hover: border 变 `border-strong`，`translateY(-1px)`。用于取消、返回等次要操作。

**Ghost (Transparent):** `background: transparent`, `border: 1px solid transparent`。Hover: 背景出现，微右移 `translateX(3px)`（导航项）或微升 `translateY(-1px)`（工具栏）。用于导航列表项、工具栏按钮。

**Danger (Ruby-Tinted):** `background: #fff1f2`, `border: 1px solid #fecaca`, `color: var(--ruby)`。Hover: 背景加深至 `#ffe4e6`。用于取消运行、删除等危险操作。

**Icon-Only:** 32×32px 或 28×28px，`border: 1px solid var(--border)`, `border-radius: 6px`, `display: grid; place-items: center`。Hover: 背景变 surface-muted，border 变 strong。

**Segmented Toggle:** 由等宽按钮组成的按钮组，外层 `background: var(--surface-muted)`, `border: 1px solid var(--border)`, `border-radius: 6px`, `padding: 2px`。激活项: `background: var(--surface)`, `box-shadow: inset 0 0 0 1px var(--border)`。

### Inputs

**Text Input:** `border: 1px solid var(--border)`, `border-radius: 6–10px`, `background: var(--surface-strong)`, `padding: 9px 11px`, `font-size: 13px`。Focus: border 变 `var(--ink)` 或 `var(--brand)`，添加 ring shadow。Placeholder: `color: var(--muted)`（与白底 ≥4.5:1 对比度）。

**Textarea:** 同上，额外 `line-height: 1.65`, `resize: vertical`, `min-height: 44px`。

**Composer Box:** 特殊变体——输入框和发送按钮在同一个容器内。容器 `border: 1px solid var(--border)`, `border-radius: 7px`, `background: var(--surface)`, `box-shadow: var(--shadow-sm)`。Focus-within: border 变 `var(--ink)`，添加 ring，`translateY(-1px)`。

**Select:** 外观同 Input，额外自定义下拉箭头 SVG，右侧 padding 30px 为箭头留空间。

### Cards / Containers

**Corner Style:** 6–8px 圆角。内容卡片 6px，面板 8px，侧边栏 10px。

**Background:** `var(--surface)`，内层容器可用 `var(--surface-muted)` 或 `var(--surface-strong)` 制造层次。

**Shadow Strategy:** 默认无阴影，仅 border 分隔。Hover 时 border 变 strong，可选 `translateY(-1px)`。不嵌套阴影卡片。

**Border:** `1px solid var(--border)`，hover 变 `var(--border-strong)`。

**Internal Padding:** 12px 标准，密集列表项 9px，宽松区域 14–16px。

### Chips / Tags

**Style:** `background: var(--surface-muted)`, `border: 1px solid var(--border)`, `border-radius: 6px`, `padding: 5px 8px`, `font-size: 11px`, `font-weight: 680–700`。用于快速模式选择、场景标签、Agent 角色类型。

**Selected State:** background 变为 `var(--surface)`，border 变 `var(--border-strong)`，可选 `inset 0 0 0 1px var(--border)` 作为 inner ring。

### Navigation

**Sidebar Nav Items:** ghost button 风格，`padding: 7px 10px`, `border-radius: 8px`。Hover: 背景 `var(--surface)`，`translateX(3px)`。Active: gradient 背景 `linear-gradient(90deg, var(--brand-soft), var(--surface))` + 左边框色 `rgba(122, 79, 19, 0.22)`。

**Tab Bar:** `display: grid`, `background: var(--page-warm)`, `border: 1px solid var(--border)`, `border-radius: 6px`, `padding: 3px`。Tab: `background: transparent`, `border-radius: 4px`。Active tab: `background: var(--surface)`, `inset 0 0 0 1px var(--border)`。

**Sub-Tab (Pill Selector):** 小号 pill 按钮组。Active: `background: var(--ink)`, `color: #fff`。

### Messages (Chat Cells)

**User Message:** `background: #1f2328`, `color: #fff`, `align-self: flex-end`, `border-radius: 8px 8px 4px 8px`, `max-width: min(820px, 88%)`。

**Assistant Message:** `background: var(--surface)`, `border: 1px solid var(--border)`, `align-self: flex-start`, `border-radius: 8px 8px 8px 4px`。左侧有一条 3px 的 `var(--border-strong)` 色条作为视觉锚点（`::before` 伪元素）。Hover: border 变 strong，微升。

**Tool Call:** 同上结构但带 header（等宽字体工具名 + 运行状态指示灯）。Error 变体：border 变 `#fecaca`，背景 `#fff1f2`。

**Approval:** amber-tinted 警告风格，`background: var(--amber-soft)`, `border: 1px solid #efd681`，半透明参数区域。Resolved 状态降低 opacity 至 0.65。

### Toast

固定在右上角，`z-index: 9999`。Slide-in 从右侧进入（280ms ease）。Error: ruby 配色。Success: 绿色。Info: indigo 配色。移除时 slide-out 向右退出（300ms ease-in）。

### Modal

**Scrim:** `position: fixed; inset: 0; background: rgba(24, 33, 31, 0.34); backdrop-filter: blur(8px)`。

**Modal Card:** `width: min(480px, 100%)`, `border-radius: 10px`, `background: var(--surface)`, `box-shadow: var(--shadow-md)`, `padding: 28px`。右上角圆形关闭按钮。

**Primary Action:** gradient 背景（`linear-gradient(135deg, var(--brand-strong), var(--brand), #b8892c)`），带暖色金 glow shadow。这是系统中唯一使用渐变的地方。

## 6. Do's and Don'ts

### Do:
- **Do** 用 `var(--ink)` 作为唯一的强调色。系统不需要第二个品牌色。
- **Do** 用粗细（650–900）和尺寸（10–18px）构建层级，不引入第二个字体族。
- **Do** 让按钮 hover 时微升（`translateY(-1px)`）、press 时下沉（`scale(0.98)`）。
- **Do** 让表面在静止时保持平坦（flat-by-default），阴影只在交互反馈或全局叠加层出现。
- **Do** 用 `var(--amber-soft)` + amber 边框表示警告/审批等待，用 `var(--ruby-soft)` + ruby 边框表示错误。
- **Do** 保持行宽 ≤75ch（约 820px）以保证长篇阅读舒适。
- **Do** 让动效可以华丽——spring 曲线、弹性入场、pulse 呼吸——只要不打断创作心流。

### Don't:
- **Don't** 引入第二个品牌色或彩色渐变（modal primary button 是唯一例外）。
- **Don't** 使用游戏/主播风格——霓虹色、暗色配高饱和强调色、激进渐变、游戏字体。
- **Don't** 使用 SaaS 营销模板套路——Hero 大图、渐变 CTA、卡片阵列、social proof。
- **Don't** 使用传统企业软件风格——密集表格、拥挤工具栏、灰底灰字、无留白。
- **Don't** 在 sidebar 以外的任何地方使用 `backdrop-filter: blur()`。玻璃材质是导航层的专属语言。
- **Don't** 嵌套阴影卡片。一层卡片 = 一个阴影或边框。内层用背景色变化区分。
- **Don't** 在每个 section 上方添加 tiny uppercase eyebrow。Label 只用于表单字段和 Inspector 标题。
- **Don't** 使用 `border-left` 或 `border-right` > 1px 作为彩色装饰条。用完整边框或背景色变化替代。
- **Don't** 使用 gradient text（`background-clip: text`）。用单一颜色和粗细表达强调。
