# Complete Single-Agent TUI Design

## Status

Validated design for improving Merak's TUI on `main`, using Astra's TUI as a
reference without importing its full product scope.

## Goals

- Preserve Merak's split `merak serve` and `merak tui` architecture.
- Keep the inline viewport and terminal-native scrollback interaction model.
- Add end-to-end streamed reasoning display.
- Add per-session model overrides controlled from the TUI.
- Provide a practical context overview panel.
- Replace single-item approval prompts with a robust approval queue and
  workspace-scoped persistent rules.
- Improve maintainability by separating runtime event translation, view
  navigation, and permission policy from the current large control paths.

## Non-Goals

- Task board, teammate tree, multi-agent approval forwarding, mentions, skill
  popup, diff viewer, global permission rules, or a full-screen terminal app.
- Copying Astra's entire TUI architecture.
- A detailed context browser that itemizes every message, memory, and tool
  result.

## Architecture

Merak keeps its HTTP + SSE runtime boundary:

```text
merak tui
  |
  | HTTP + SSE
  v
merak serve
  |
  +-- runtime
  +-- agent loop
  +-- providers
  +-- tools and permission policy
```

The TUI remains an inline terminal client. Committed history cells flush to
native terminal scrollback once. One active cell is redrawn above the composer
until it is finalized.

### TUI Modules

- `ReasoningCell`: renders live reasoning separately from the assistant answer.
  It finalizes on `reasoning_completed` or on the first answer delta.
- `TurnSummaryCell`: emits an end-of-turn band with elapsed time, token counts,
  tool count, and cumulative usage.
- `ContextPanel`: renders current prompt usage, model limit, cumulative input
  and output, message count, tool call count, and completed turns.
- `ModelPickerView`: lists supported server models and sets a session override.
- `ApprovalQueue`: owns pending approval entries, focus, deduplication, and
  group operations.
- `PermissionRuleStore`: reads and writes workspace-scoped permission rules.
- `ViewStack`: manages help, context, model picker, transcript, tool browser,
  tool detail, and approval views with consistent open, close, and navigation
  behavior.
- `RuntimeEventBridge`: translates SSE payloads into typed TUI events.

`ScreenManager` remains responsible for the terminal event loop, input
dispatch, drawing, and view navigation. Runtime HTTP calls remain in the client
layer. The bridge keeps wire-format handling out of `main.cpp`.

### Server Modules

- Provider streaming surfaces reasoning deltas when the selected provider
  supports them.
- Runtime persists and broadcasts reasoning events alongside existing run
  events.
- Session state stores an optional model override.
- Permission policy classifies tool risk and checks workspace rules before an
  approval is requested.

## Runtime Events

The public runtime stream adds:

```text
reasoning_delta
reasoning_completed
approval_resolved
```

Existing events remain:

```text
run_started
state_changed
text_delta
usage_updated
tool_started
tool_completed
approval_requested
run_completed
run_failed
run_cancelled
run_interrupted
```

The TUI bridge maps runtime payloads to typed UI events. Its cell transitions
follow these rules:

1. The first `reasoning_delta` starts a live `ReasoningCell`.
2. Later reasoning deltas append to the same cell.
3. `reasoning_completed` finalizes the reasoning cell.
4. The first `text_delta` also finalizes reasoning if needed, then starts a live
   `AssistantCell`.
5. Tool start finalizes any active cell and starts a `ToolCell`.
6. Tool completion updates and finalizes the matching tool cell.
7. Run completion finalizes any active cell and adds a `TurnSummaryCell`.

Session replay consumes journal events using the same bridge. Completed cells
are rebuilt and marked as flushed after they are painted once. An unfinished
run is represented as interrupted after server recovery.

## Session Model Override

Model switching is scoped to one session and does not modify local settings.

New endpoints:

```text
GET  /v1/models
POST /v1/sessions/{id}/model
```

`GET /v1/models` returns the available models, current default, provider, and
useful metadata such as context limit when known. The model update endpoint
stores or clears a session override.

When a run starts, runtime selects:

```text
session model override ?? configured default model
```

`/model` opens `ModelPickerView`. Selecting a model updates the session through
the runtime client and appends a system cell describing the change.

## Context Panel

`/context` displays a practical overview:

```text
Current prompt usage
Model context limit
Cumulative input tokens
Cumulative output tokens
Messages
Completed turns
Tool calls
```

The panel uses usage events already received by the TUI plus runtime model
metadata. Unknown values render as unavailable rather than as zero.

## Approval Queue

### Approval Payload

The server approval request includes:

```text
approval_id
tool_name
arguments
summary
risk_level
risk_tags
batch_group
workspace_root
```

### Queue Behavior

- Equivalent approval requests are deduplicated using tool name, normalized
  arguments, workspace root, and source run.
- Deduplicated requests share one visible entry and receive the same response.
- Up and down change the focused request.
- Left and right change the focused action.
- `Enter` activates the focused action.
- `y` performs `Allow once`; `n` performs `Deny`.
- `Esc` leaves the request unresolved and returns focus to the composer only
  when the active view permits it.
- The status line displays the pending approval count.

Actions:

```text
Allow once
Always allow workspace
Allow group
Deny
Deny group
```

High-risk requests disable `Always allow workspace` and `Allow group`.
Group denial remains available.

### Workspace Rules

Rules are stored at:

```text
<workspace>/.merak/permissions.json
```

Only stable, safely matchable targets can become persistent rules. Examples
include a single tool with a canonical path scope. Compound shell commands,
destructive Git operations, deletion commands, and unsafe argument shapes
cannot generate permanent allow rules.

Malformed rule files are treated as empty rule sets. The TUI appends a warning
system cell so the failure remains visible without blocking the run.

## Layout And Interaction

The bottom viewport remains compact:

```text
live reasoning / assistant / tool
--------------------------------
approval queue or overlay
composer
shortcut hint | provider/model | state | tokens | approvals
```

Keyboard behavior:

- `Enter`: submit composer input or activate the selected view action.
- `Shift+Enter`: add a composer newline.
- `Ctrl+C`: cancel an active run, clear a non-empty draft, or exit while idle.
- `Ctrl+D`: exit while idle with an empty composer.
- `Ctrl+L`: force redraw.
- `Ctrl+O`: open transcript.
- `Ctrl+T`: open tool browser.
- `Ctrl+R`: restore the previous user input into the composer for editing.
- `F1`: open help.
- Arrow keys: navigate popups, views, and approval actions.
- `Esc`: close the current view where supported.
- `y` and `n`: approval shortcuts.

`/model`, `/context`, `/help`, `/transcript`, and `/tool-calls` use `ViewStack`.
Help presents grouped commands and keyboard shortcuts. Narrow terminals shorten
the status line before hiding interactive content. The composer remains
visible.

## Error Handling

- Unknown or unsupported reasoning fields are ignored without affecting answer
  streaming.
- A provider that does not emit reasoning still completes normally.
- Model override errors append a system error cell and keep the previous model.
- Runtime reconnect resumes SSE from the last sequence number.
- Invalid permission rule files produce a visible warning and behave as empty.
- Failed tool executions remain visible as failed `ToolCell` entries.
- Batch approval operations apply only to entries with the same safe batch
  group.

## Testing

### Unit Tests

- Reasoning cell streaming and finalization.
- Turn summary formatting.
- Event bridge transitions from reasoning to answer and from tool completion to
  run summary.
- Session model override selection and fallback.
- Context panel known and unavailable values.
- Permission rule parsing, serialization, and malformed-file fallback.
- Risk classification and unsafe permanent-rule rejection.
- Approval deduplication, focus navigation, shared response delivery, and safe
  batch operations.

### TUI Rendering Tests

Render deterministic lines at fixed terminal widths for:

- Live and finalized reasoning.
- Canonical turn with reasoning, answer, tool call, and summary.
- Approval queue with enabled and disabled actions.
- Context panel.
- Model picker.
- Narrow status line.

### Runtime Contract Tests

- Reasoning SSE event ordering and journal replay.
- Session model override endpoint and new-run selection.
- Approval rule hit bypasses prompt.
- Denied approval prevents tool execution.
- High-risk requests cannot create persistent allow rules.

## Implementation Boundary

This design is one implementation plan with staged delivery:

1. Add tests and typed runtime event bridge.
2. Add reasoning events end to end and turn summaries.
3. Add model catalog and session override API plus picker.
4. Introduce `ViewStack` and context panel.
5. Add permission policy, rule store, approval queue, and rendering.
6. Run focused tests, complete suite, and terminal-level smoke verification.

