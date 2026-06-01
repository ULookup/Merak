# TUI Semantic Colors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add restrained semantic colors to the fullscreen TUI while preserving Unicode width behavior.

**Architecture:** Store a lightweight semantic style with each chat timeline row and translate that style into FTXUI decorators during rendering. Render the Welcome banner as styled segments so its border, decorative glyphs, metadata, and tips can use distinct colors without embedding ANSI escapes in text.

**Tech Stack:** C++23, FTXUI

---

### Task 1: Add Shared TUI Palette

**Files:**
- Create: `cli/src/tui/colors.hpp`

- [ ] Define Palette256 colors for accent gold, light gold, muted gold, blue, green, and red.
- [ ] Keep palette constants independent of ANSI REPL escape sequences.

### Task 2: Style Chat Timeline Rows

**Files:**
- Modify: `cli/src/tui/panels/chat_panel.hpp`

- [ ] Replace plain timeline strings with rows carrying text and semantic style.
- [ ] Render user input prefixes in gold, assistant text in white, tool-running rows in blue, successful tool rows in green, failed tool rows in red, and token summaries in muted gold.
- [ ] Preserve streaming append behavior and tool-row updates by index.

### Task 3: Style Welcome Banner Segments

**Files:**
- Modify: `cli/src/tui/welcome.hpp`
- Modify: `cli/src/tui/panels/chat_panel.hpp`

- [ ] Add an API for inserting a row composed of styled segments.
- [ ] Render Welcome borders and title in accent gold.
- [ ] Render decorative stars and mountain outline in light gold.
- [ ] Render MERAK glyph rows in white.
- [ ] Render provider/model metadata and tips in muted gold.

### Task 4: Add Status Bar Semantic Colors

**Files:**
- Modify: `cli/src/tui/components/status_bar.hpp`

- [ ] Render provider, model, and usage in muted gold.
- [ ] Render idle state in muted gold, active phases in blue, approval waiting in accent gold, and error state in red.

### Task 5: Verify

- [ ] Run `git diff --check`.
- [ ] Validate modified Unicode source files as UTF-8 without replacement characters.
- [ ] Record that Linux build and visual verification require a Linux toolchain unavailable on this machine.
