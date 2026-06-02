# Astra-Style Merak TUI Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild Merak's TUI around inline native scrollback, live status, queued input, lightweight approvals, and compact sub-agent activity.

**Architecture:** Keep FTXUI 5.0.0 and use `ScreenInteractive::TerminalOutput()`. Route typed worker events into a main-thread `ChatModel`; drain committed `HistoryCell` values to terminal scrollback exactly once and render only live state in the viewport.

**Tech Stack:** C++23, FTXUI 5.0.0, CMake, Conan, plain assertion-based tests

---

### Task 1: TUI Model

- [x] Add failing tests for history cells, status timing, queued composer input, approvals, and sub-agent rows.
- [x] Add focused model headers under `cli/src/tui/`.
- [ ] Run the TUI model tests. Blocked locally: CMake, Conan, and a C++ compiler are unavailable.

### Task 2: Loop Events

- [x] Add failing tests for cancellation and sub-agent observer callbacks.
- [x] Extend `AgentLoop` with best-effort cancellation checks.
- [x] Extend `SubAgentRunner` with observer events.
- [ ] Run loop tests. Blocked locally: CMake, Conan, and a C++ compiler are unavailable.

### Task 3: Inline Runtime

- [x] Replace fullscreen rendering with `ScreenInteractive::TerminalOutput()`.
- [x] Route worker callbacks through typed events and main-thread mutation.
- [x] Flush committed cells through restored terminal I/O.
- [x] Preserve overlays, approval handling, token usage, and the welcome banner.

### Task 4: Verification

- [ ] Configure with Conan and CMake. Blocked locally: the toolchain is unavailable.
- [ ] Run `ctest --output-on-failure`. Blocked locally: no configured build exists.
- [ ] Manually inspect the Linux TTY flow for scrollback, streaming, animation,
      approvals, queued drafts, cancellation, overlays, welcome banner, and
      narrow terminals.
