# UI Visual Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Align WebUI visual implementation with DESIGN.md — unify DesktopShell colors, eliminate hardcoded values, clean up misleading variable names, and add animation tokens.

**Architecture:** 4-layer incremental plan. Layer 1 cleans global.css (add `--border-subtle`, deprecated aliases, animation tokens). Layer 2 rewrites DesktopShell from hardcoded blue to glass/blur warm system. Layer 3 replaces 30+ hardcoded `rgba()` values and PipelineNavigator colors with CSS variables. Layer 4 adds page/agent/tab transition animations.

**Tech Stack:** React 19, TypeScript, CSS Modules, Vite, vitest

---

## File Structure Map

```
src/styles/global.css          — :root tokens (Layer 1)
src/shell/DesktopShell.module.css  — shell styling (Layer 2)
src/shell/DesktopShell.tsx      — shell component (Layer 2, optional)
src/App.tsx                     — page transition (Layer 4)
src/App.module.css              — border-subtle + editor exit (Layer 3+4)
src/components/MainPanel.tsx    — agent switch animation (Layer 4)
src/components/MainPanel.module.css — border-subtle (Layer 3)
src/components/InspectorPanel.tsx   — tab crossfade (Layer 4)
src/components/InspectorPanel.module.css — border-subtle (Layer 3)
src/components/Composer.module.css — border-subtle (Layer 3)
src/components/ChatTimeline.module.css — border-subtle (Layer 3)
src/components/WorldSidebar.module.css — border-subtle (Layer 3)
src/components/WorldDashboard.module.css — border-subtle (Layer 3)
src/components/Skeleton.module.css — border-subtle (Layer 3)
src/components/cells/Cells.module.css — border-subtle (Layer 3)
src/components/Sidebar/PipelineNavigator.module.css — hardcoded colors (Layer 3)
src/components/ChapterEditor.tsx — exit animation (Layer 4)
src/components/ChapterEditor.module.css — --hover → hardcoded (Layer 3)
src/components/HelpDrawer.module.css — --brand-wash (Layer 3)
```

---

## Layer 1: Global CSS Cleanup

### Task 1.1: Add `--border-subtle` and animation tokens, mark deprecated aliases

**Files:**
- Modify: `src/styles/global.css` (:root block)

- [ ] **Step 1: Add new tokens and deprecated comments**

```css
/* In :root block, after existing tokens: */

/* ---- Subtle border (replaces hardcoded rgba(224,223,220,0.92) in components) ---- */
--border-subtle: rgba(224, 223, 220, 0.92);

/* ---- Animation tokens ---- */
--ease-out: cubic-bezier(0.4, 0, 0.2, 1);
--anim-lift-sm: translateY(-1px);
--anim-lift-md: translateY(-3px);
--anim-press: scale(0.98);
--anim-fade-in: 200ms var(--ease) both;
```

- [ ] **Step 2: Add deprecated comments on aliases (do not remove — components still reference them)**

```css
/* In :root block, add comment above --teal/--indigo/--violet/--purple/--brand-wash group: */

/* ⚠️ DEPRECATED aliases — kept for backward compat. New code use --ink / --muted-strong / --surface-muted directly. */
--teal: #171717;
--teal-strong: #171717;
--teal-soft: #ededeb;
--teal-wash: #f7f7f5;
--brand-wash: #f7f7f5;
--indigo: #4a4a4a;
--indigo-soft: #eeeeec;
--violet: #62605b;
--violet-soft: #f0efed;
--purple: #62605b;
--purple-soft: #f0efed;
```

- [ ] **Step 3: Verify build passes**

Run: `cd /home/icepop/Merak/webui && npm run build 2>&1 | tail -5`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add src/styles/global.css
git commit -m "style: add --border-subtle and animation tokens, mark deprecated aliases"
```

---

## Layer 2: DesktopShell Visual Unification

### Task 2.1: Rewrite DesktopShell CSS — hardcoded blue → CSS variables + glass

**Files:**
- Modify: `src/shell/DesktopShell.module.css`

- [ ] **Step 1: Replace sidebar background and border**

In `.sidebar` (around line 23), replace:
```css
.sidebar {
  /* ... keep grid/flex/sizing ... */
  border-right: 1px solid #e2e8f0;
  background: #fff;
}
```
With:
```css
.sidebar {
  /* ... keep grid/flex/sizing ... */
  border-right: 1px solid var(--border);
  background: rgba(255, 255, 255, 0.82);
  backdrop-filter: blur(18px);
}
```

- [ ] **Step 2: Replace worldCard, search, worldSelector border/background colors**

```css
.worldCard,
.search,
.worldSelector {
  display: flex;
  align-items: center;
  gap: 9px;
  border: 1px solid var(--border-subtle);
  border-radius: 7px;
  background: var(--surface);
  color: var(--ink);
}
```

Remove the old hardcoded declarations:
```css
/* DELETE these old lines: */
/* border: 1px solid #dce3ed; */
/* background: #fff; */
/* color: #17213b; */
```

- [ ] **Step 3: Replace nav button colors**

In `.navButton`:
```css
.navButton {
  /* ... keep existing layout ... */
  background: transparent;
  color: var(--muted-strong);
  /* ... keep transitions ... */
}
```

In `.navButton:hover`:
```css
.navButton:hover {
  background: var(--surface);
  color: var(--ink);
}
```

In `.navButton[aria-current='page']`:
```css
.navButton[aria-current='page'] {
  background: linear-gradient(90deg, var(--brand-soft), var(--surface));
  color: var(--ink);
  font-weight: 700;
  box-shadow: inset 2px 0 0 var(--ink);
}
```

- [ ] **Step 4: Replace topbar colors**

In `.topbar`:
```css
.topbar {
  /* ... keep layout ... */
  border-bottom: 1px solid var(--border-subtle);
  background: rgba(255, 255, 255, 0.94);
  backdrop-filter: blur(14px);
}
```

- [ ] **Step 5: Replace search and kbd colors**

In `.search`:
```css
.search {
  /* ... keep layout ... */
  color: var(--muted);
  /* REMOVE: color: #667085; */
}
```

In `.search kbd`:
```css
.search kbd {
  /* ... keep layout ... */
  border: 1px solid var(--border);
  border-radius: 4px;
  background: var(--surface-muted);
  /* REMOVE: border: 1px solid #e3e8ef; */
  /* REMOVE: background: #f8fafc; */
}
```

- [ ] **Step 6: Replace worldSelector border**

In `.worldSelector`:
```css
.worldSelector {
  /* ... keep layout ... */
  border: 1px solid var(--border-subtle);
  /* REMOVE: border: 1px solid #dce3ed; */
}
```

- [ ] **Step 7: Replace mobile nav backdrop color**

In `.navBackdrop`:
```css
.navBackdrop {
  /* ... keep layout ... */
  background: rgba(23, 23, 23, 0.38);
  /* REMOVE: background: rgb(15 23 42 / 38%); */
}
```

- [ ] **Step 8: Replace mobile menu/close button colors**

In `.menuButton` and `.navClose`:
```css
.menuButton,
.navClose {
  /* ... keep layout ... */
  border: 1px solid var(--border);
  background: var(--surface);
  color: var(--muted-strong);
  /* REMOVE: color: var(--brand); */
}
```

- [ ] **Step 9: Remove `--shell-navy` declaration**

In `.shell`:
```css
.shell {
  /* REMOVE: --shell-navy: var(--brand); */
  display: grid;
  grid-template-columns: 244px minmax(0, 1fr);
  /* ... keep rest ... */
}
```

- [ ] **Step 10: Verify build passes**

Run: `cd /home/icepop/Merak/webui && npm run build 2>&1 | tail -5`
Expected: no errors

- [ ] **Step 11: Commit**

```bash
git add src/shell/DesktopShell.module.css
git commit -m "style: unify DesktopShell with warm monochrome glass/blur language"
```

---

## Layer 3: Component Hardcoded Color Replacement

### Task 3.1: Bulk replace `rgba(224, 223, 220, 0.92)` with `var(--border-subtle)`

**Files:** 10 files, ~30 occurrences

- [ ] **Step 1: Run search to confirm exact count**

```bash
grep -rn 'rgba(224, 223, 220' src/ --include='*.css' | wc -l
```

- [ ] **Step 2: Bulk replace using sed**

```bash
cd /home/icepop/Merak/webui
find src/ -name '*.css' -exec sed -i 's/rgba(224, 223, 220, 0\.92)/var(--border-subtle)/g' {} +
find src/ -name '*.css' -exec sed -i 's/rgba(224, 223, 220, 0\.9)/var(--border-subtle)/g' {} +
find src/ -name '*.css' -exec sed -i 's/rgba(224, 223, 220, 0\.88)/var(--border-subtle)/g' {} +
```

- [ ] **Step 3: Verify no remaining occurrences**

```bash
grep -rn 'rgba(224, 223, 220' src/ --include='*.css'
```
Expected: no output

- [ ] **Step 4: Verify build passes**

Run: `cd /home/icepop/Merak/webui && npm run build 2>&1 | tail -5`
Expected: no errors

- [ ] **Step 5: Commit**

```bash
git add -u src/
git commit -m "style: replace hardcoded border-subtle rgba() with CSS variable"
```

### Task 3.2: Fix PipelineNavigator hardcoded colors

**Files:**
- Modify: `src/components/Sidebar/PipelineNavigator.module.css`

- [ ] **Step 1: Replace error banner colors**

In `.errorBanner`:
```css
.errorBanner {
  background: var(--ruby-soft);
  border: 1px solid var(--ruby);
  /* ... keep rest ... */
  color: var(--ruby);
}
```
Remove: `background: #fff0f0; border: 1px solid #e00; color: #c00;`

In `.errorBanner button`:
```css
.errorBanner button {
  background: none;
  border: none;
  color: var(--ruby);
  /* ... keep rest ... */
}
```
Remove: `color: #c00;`

- [ ] **Step 2: Replace complete banner colors**

In `.completeBanner`:
```css
.completeBanner {
  background: var(--green-soft);
  border: 1px solid var(--green);
  /* ... keep rest ... */
  color: var(--green);
}
```
Remove: `background: #f0fff0; border: 1px solid #4caf50; color: #2e7d32;`

In `.completeBanner button`:
```css
.completeBanner button {
  background: none;
  border: none;
  color: var(--green);
  /* ... keep rest ... */
}
```
Remove: `color: #2e7d32;`

- [ ] **Step 3: Replace confirm dialog button colors**

In `.confirmBtn`:
```css
.confirmBtn {
  /* ... keep layout ... */
  background: var(--ink);
  color: #fff;
}
```
Remove: `background: #4a90d9;`

In `.confirmDialog`:
```css
.confirmDialog {
  background: var(--surface);
  /* ... keep rest ... */
}
```
Remove: `background: #fff;`

- [ ] **Step 4: Replace condMet dot color**

In `.condMet`:
```css
.condMet {
  background: var(--green);
}
```
Remove: `background: var(--teal-soft, #4a8);`

- [ ] **Step 5: Verify build passes**

Run: `cd /home/icepop/Merak/webui && npm run build 2>&1 | tail -5`
Expected: no errors

- [ ] **Step 6: Commit**

```bash
git add src/components/Sidebar/PipelineNavigator.module.css
git commit -m "style: replace PipelineNavigator hardcoded colors with CSS variables"
```

### Task 3.3: Fix ChapterEditor `--hover` and `--violet` references

**Files:**
- Modify: `src/components/ChapterEditor.module.css`

- [ ] **Step 1: Replace `var(--hover)` with hardcoded neutral hover**

In `.contextBtn:hover, .reviewBtn:hover` and `.entityTag:hover`:
```css
.contextBtn:hover,
.reviewBtn:hover {
  background: rgba(23, 23, 23, 0.055);
  /* ... keep rest ... */
}
```
Remove: `background: var(--hover);`

In `.entityTag:hover`:
```css
.entityTag:hover {
  background: rgba(23, 23, 23, 0.055);
  /* ... keep rest ... */
}
```
Remove: `background: var(--hover);`

- [ ] **Step 2: Replace `var(--violet)` with `var(--muted-strong)`**

In `.entityTag.secret`:
```css
.entityTag.secret {
  border-color: var(--muted-strong);
  color: var(--muted-strong);
}
```
Remove: `border-color: var(--violet); color: var(--violet);`

- [ ] **Step 3: Verify build passes**

Run: `cd /home/icepop/Merak/webui && npm run build 2>&1 | tail -5`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add src/components/ChapterEditor.module.css
git commit -m "style: replace --hover and --violet with explicit values in ChapterEditor"
```

### Task 3.4: Fix `--brand-wash` reference in HelpDrawer

**Files:**
- Modify: `src/components/HelpDrawer.module.css`

- [ ] **Step 1: Replace `var(--brand-wash)` with `var(--surface-muted)`**

```css
/* In .someClass, line ~131: */
/* background: var(--brand-wash); → background: var(--surface-muted); */
```

- [ ] **Step 2: Verify build and commit**

```bash
cd /home/icepop/Merak/webui && npm run build 2>&1 | tail -5
git add src/components/HelpDrawer.module.css
git commit -m "style: replace --brand-wash with --surface-muted in HelpDrawer"
```

---

## Layer 4: Interaction Transitions & Animations

### Task 4.1: Add page transition animation to App.tsx

**Files:**
- Modify: `src/App.module.css`

- [ ] **Step 1: Add page transition keyframes**

Append to `src/App.module.css`:
```css
@keyframes pageFadeIn {
  from {
    opacity: 0;
    transform: translateY(6px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

@keyframes pageFadeOut {
  from {
    opacity: 1;
    transform: translateY(0);
  }
  to {
    opacity: 0;
    transform: translateY(-6px);
  }
}

.editorExit {
  animation: pageFadeOut 280ms var(--ease-spring) forwards;
}

.pageEnter {
  animation: pageFadeIn 280ms var(--ease-spring) both;
}
```

- [ ] **Step 2: Add `pageEnter` class to workspace element in App.tsx**

In `src/App.tsx`, the three-column workbench section (around line 458), add `className={styles.pageEnter}` to the workspace div:

```tsx
// OLD:
<div className={styles.workspace}>

// NEW:
<div className={`${styles.workspace} ${styles.pageEnter}`}>
```

- [ ] **Step 3: Add editor exit animation to App.tsx**

In the editor overlay section (around line 312), wrap the exit in a stateful animation. At the top of `AppInner`, add:

```tsx
const [exitingEditor, setExitingEditor] = useState(false);
```

Modify the editor overlay div to support exit animation:

```tsx
// OLD at around line 312:
<div className={styles.editorOverlay}>

// NEW:
<div className={`${styles.editorOverlay} ${exitingEditor ? styles.editorExit : ''}`}>
```

Wrap the `safeNavigate('overview')` call in editor's back button:

The back button already calls `safeNavigate('overview')`. To add animation, we'd need to intercept. Skip this — the overlay is replaced instantly when state changes. The exit animation can be added later if needed.

Instead, just add the keyframe definitions. The `editorOverlay` already has `animation: pageIn 280ms` for entry.

- [ ] **Step 4: Verify build passes**

Run: `cd /home/icepop/Merak/webui && npm run build 2>&1 | tail -5`
Expected: no errors

- [ ] **Step 5: Commit**

```bash
git add src/App.module.css src/App.tsx
git commit -m "feat: add page transition animation keyframes"
```

### Task 4.2: Add Inspector tab crossfade transition

**Files:**
- Modify: `src/components/InspectorPanel.module.css`

- [ ] **Step 1: Add crossfade to content area**

In `.content`:
```css
.content {
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding-right: 2px;
  animation: tabFadeIn 180ms var(--ease) both;
}
```

Add keyframe:
```css
@keyframes tabFadeIn {
  from {
    opacity: 0;
    transform: translateX(4px);
  }
  to {
    opacity: 1;
    transform: translateX(0);
  }
}
```

Also add reduced-motion override:
```css
@media (prefers-reduced-motion: reduce) {
  .content {
    animation: none;
  }
}
```

- [ ] **Step 2: Verify build and commit**

```bash
cd /home/icepop/Merak/webui && npm run build 2>&1 | tail -5
git add src/components/InspectorPanel.module.css
git commit -m "feat: add Inspector tab content crossfade transition"
```

### Task 4.3: Add Agent switch crossfade to MainPanel

**Files:**
- Modify: `src/components/ChatTimeline.module.css`

- [ ] **Step 1: Add agent-switch animation**

Append to `src/components/ChatTimeline.module.css`:
```css
.agentSwitch {
  animation: agentFadeIn 200ms var(--ease) both;
}

@keyframes agentFadeIn {
  from {
    opacity: 0;
    transform: translateY(4px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

@media (prefers-reduced-motion: reduce) {
  .agentSwitch {
    animation: none;
  }
}
```

- [ ] **Step 2: Apply class when agentId changes in MainPanel.tsx**

In `src/components/MainPanel.tsx`, add a key prop to ChatTimeline based on agentId:

```tsx
// OLD around line 169:
<ChatTimeline connectionState={connectionState} />

// NEW:
<ChatTimeline key={state.agentId} connectionState={connectionState} className={styles.agentSwitch} />
```

Actually CSS modules don't pass className through like that directly. Instead, wrap in a div:

```tsx
<div key={state.agentId} className={styles.agentSwitch}>
  <ChatTimeline connectionState={connectionState} />
</div>
```

But we need to import the style. Actually MainPanel already imports `styles from './MainPanel.module.css'`. Let's add the keyframe to MainPanel.module.css instead:

Append to `src/components/MainPanel.module.css`:
```css
.agentSwitch {
  animation: agentFadeIn 200ms var(--ease) both;
}

@keyframes agentFadeIn {
  from {
    opacity: 0;
    transform: translateY(4px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

@media (prefers-reduced-motion: reduce) {
  .agentSwitch {
    animation: none;
  }
}
```

Then in MainPanel.tsx, wrap ChatTimeline:
```tsx
<div key={state.agentId} className={styles.agentSwitch}>
  <ChatTimeline connectionState={connectionState} />
</div>
```

- [ ] **Step 3: Verify build and commit**

```bash
cd /home/icepop/Merak/webui && npm run build 2>&1 | tail -5
git add src/components/MainPanel.module.css src/components/MainPanel.tsx
git commit -m "feat: add agent switch crossfade animation in MainPanel"
```

### Task 4.4: Run full test suite

- [ ] **Step 1: Run tests**

```bash
cd /home/icepop/Merak/webui && npm test 2>&1
```

Expected: All tests pass (no failures introduced by CSS/style changes)

---

## Summary

**Total Tasks:** 8
**Total Files Modified:** ~15
**Delivery Order:**

| Commit | Content |
|--------|---------|
| 1 | `style: add --border-subtle and animation tokens, mark deprecated aliases` |
| 2 | `style: unify DesktopShell with warm monochrome glass/blur language` |
| 3 | `style: replace hardcoded border-subtle rgba() with CSS variable` |
| 4 | `style: replace PipelineNavigator hardcoded colors with CSS variables` |
| 5 | `style: replace --hover and --violet with explicit values in ChapterEditor` |
| 6 | `style: replace --brand-wash with --surface-muted in HelpDrawer` |
| 7 | `feat: add page transition animation keyframes` |
| 8 | `feat: add Inspector tab content crossfade transition` |
| 9 | `feat: add agent switch crossfade animation in MainPanel` |

The 30+ hardcoded `rgba(224,223,220,0.92)` replacements across multiple files are grouped into a single commit (3) as they are purely mechanical.
