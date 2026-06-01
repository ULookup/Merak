# TUI Semantic Colors Design

## Goal

Add restrained semantic colors to the Merak TUI while preserving Unicode width
handling and keeping the REPL color language consistent.

## Palette

- Accent gold: borders, titles, prompts, and approval emphasis.
- Light gold: Welcome stars and mountain outline.
- White: assistant text and the main MERAK banner glyphs.
- Muted gold: tips, model metadata, token summaries, and idle status.
- Blue: active tool execution and active model phases.
- Green: successful tool completion.
- Red: failed tool completion and errors.

## Rendering Model

Chat timeline entries carry a lightweight semantic style alongside their text.
FTXUI applies colors while rendering; ANSI escape sequences are not stored in
timeline strings. This keeps Unicode width calculations stable.

The Welcome banner uses styled segments instead of flat strings so its border,
decorative mountain, MERAK glyphs, model metadata, and tips can use distinct
colors without changing the layout.

## Scope

This change is limited to TUI presentation. Agent execution, tool behavior,
token accounting, and REPL rendering remain unchanged.

## Verification

- Run `git diff --check`.
- Validate UTF-8 source files.
- Build and visually inspect the TUI on Linux when a Linux toolchain is
  available.
