# Astra-Style TUI Refactor Design

## Goal

Replace Merak's fullscreen chat renderer with an Astra-style inline terminal
viewport. Completed output must enter native terminal scrollback once, while
the live viewport remains focused on current work, input, status, and overlays.

## Architecture

The CLI keeps FTXUI 5.0.0 and uses `ScreenInteractive::TerminalOutput()`.
Worker callbacks enqueue typed `TuiEvent` values. Only the FTXUI thread mutates
the visible `ChatModel`.

`ChatModel` owns committed immutable `HistoryCell` values, one live cell, an
ephemeral approval cell, a composer, the status indicator, and compact
sub-agent rows. Newly committed cells are drained and printed once through
restored terminal I/O.

## Visible Behavior

- The existing large Merak welcome banner is flushed once at startup.
- A submitted user message immediately enters scrollback.
- The active region uses a stable `Thinking` label with turn-level elapsed
  time. Tool work appears as secondary activity such as `Running grep`.
- A silent thinking turn gains a `thought for Ns` hint after five seconds.
- Assistant output streams into one live cell and commits at turn completion.
- Completed tools commit compact cards containing name, status, duration,
  argument summary, and a short output preview.
- Approval prompts are ephemeral `ApprovalCell` values and accept `y`, `n`, or
  `Esc`.
- While a turn is active, submitted messages queue. `Up` edits the newest
  queued message. After turn completion, the next queued message returns to
  the composer for review instead of auto-running.
- `Ctrl+C` requests best-effort cancellation. A provider or tool call already
  blocked in its own API finishes before cancellation is observed.
- Compact sub-agent rows show status, id, elapsed time, and observed tool steps.

## Scope Boundaries

This phase does not add transcript persistence, resume replay, Markdown syntax
highlighting, multi-line editing, sub-agent drilldown, delegation commands, or
provider-level abort APIs.
