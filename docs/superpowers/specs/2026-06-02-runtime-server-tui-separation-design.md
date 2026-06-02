# Merak Runtime Server and TUI Separation Design

## Status

Approved on 2026-06-02.

## Goal

Split Merak into a local-first agent runtime and a TUI client. The TUI must no
longer own the agent loop, providers, tools, MCP clients, memory, or session
state. A future Web client must be able to reuse the same HTTP and SSE protocol
without another agent-core refactor.

The first release is local-only:

- `merak serve` starts a long-running server on the local machine.
- `merak tui` connects to that server.
- Tools, including MCP tools, execute on the server machine.
- The existing REPL is deleted.

This is a complete architectural migration. There is no compatibility REPL,
temporary callback adapter, reduced persistence mode, or TUI-owned fallback
agent loop.

## Non-Goals

- Web UI
- Authentication and multi-user isolation
- Remote edge tool execution
- Cloud deployment
- Durable background jobs
- Automatic continuation of active LLM or shell work after a server restart

## Architecture

```text
merak tui
   |
   | HTTP + SSE
   v
merak serve
   |
   +-- merak-http       HTTP routes, SSE encoding, error mapping
   +-- merak-runtime    sessions, runs, event bus, approvals, cancellation
   +-- merak-storage    SQLite state index, append-only JSONL journal
   +-- merak-loop       agent execution
   +-- merak-tools      local built-in tools and MCP wrappers
   +-- merak-llm        provider implementations
   +-- merak-context    context assembly and compaction
   +-- merak-memory     memory storage and retrieval
```

### `merak-runtime`

`merak-runtime` owns orchestration but not HTTP concerns.

- `RuntimeService` creates and queries sessions, starts runs, resolves
  approvals, cancels runs, and exposes event subscriptions.
- `AgentRun` owns the state of one unit of work triggered by one user message.
- `RuntimeEvent` is the stable boundary between the runtime and clients.
- `EventBus` broadcasts events to active subscribers without blocking the
  agent loop.
- `ApprovalGate` persists a pending approval and suspends tool execution until
  the request is resolved or the run is cancelled.
- `CancellationToken` is checked around LLM calls and tool execution.

`AgentLoop` must use runtime-native event, approval, and cancellation
interfaces. The runtime is not an adapter wrapped around TUI callbacks.

### `merak-storage`

`merak-storage` owns two complementary persistence mechanisms:

- SQLite stores queryable current state.
- JSONL stores the immutable event history for recovery, audit, and future
  replay.

SQLite is an index of current truth. The JSONL journal is the append-only record
of how that state was reached.

### `merak-http`

`merak-http` is a protocol adapter only:

- Convert HTTP requests into `RuntimeService` calls.
- Convert `RuntimeEvent` values into SSE frames.
- Map runtime errors into structured HTTP errors.

It must not contain agent execution rules.

### CLI

The CLI exposes exactly two interactive execution modes:

```text
merak serve
merak tui [--session <session_id>] [--server http://127.0.0.1:3888]
```

The server is started independently. The TUI does not auto-start it.

The TUI owns presentation only:

- Submit messages.
- Render historical and live events.
- Render tool activity.
- Prompt for approvals and post decisions.
- Request run cancellation.
- Reconnect to SSE and request missing events.

The TUI must not initialize or link directly to agent execution dependencies.

## HTTP API

The default server address is `127.0.0.1:3888`.

### Sessions

```text
POST /v1/sessions
GET  /v1/sessions
GET  /v1/sessions/{session_id}
GET  /v1/sessions/{session_id}/events?after={seq}
GET  /v1/sessions/{session_id}/events/stream?after={seq}
POST /v1/sessions/{session_id}/runs
```

Create a run:

```json
{
  "message": "Check the current project and fix the failing tests."
}
```

Response:

```json
{
  "run_id": "run_...",
  "session_id": "session_..."
}
```

Only one unfinished run may exist in a session. A second submission returns
`session_busy`.

### Approvals and Cancellation

```text
POST /v1/approvals/{approval_id}
POST /v1/runs/{run_id}/cancel
```

Approval request:

```json
{
  "decision": "allow"
}
```

Valid decisions are `allow` and `deny`. Resolving an already-resolved approval
is idempotent.

## Runtime Events

Each session owns a monotonically increasing `seq`. An event is appended to the
journal before it is broadcast.

First-release event types:

```text
session_created
run_started
state_changed
text_delta
tool_started
tool_completed
approval_requested
approval_resolved
run_completed
run_failed
run_cancelled
run_interrupted
```

The journal also stores internal recovery records that do not need to be sent
to clients:

```text
message_appended
compaction_applied
```

`message_appended` preserves the full provider-facing message, including tool
call IDs and provider content blocks. Rebuilding a loop after restart uses
these records rather than concatenating `text_delta` events.

Example JSONL record:

```json
{
  "seq": 44,
  "timestamp": "2026-06-02T12:00:00Z",
  "session_id": "session_...",
  "run_id": "run_...",
  "type": "tool_completed",
  "payload": {
    "tool": "execute_bash",
    "is_error": false,
    "output": "..."
  }
}
```

Example SSE frame:

```text
id: 44
event: tool_completed
data: {"run_id":"run_...","tool":"execute_bash","is_error":false,"output":"..."}
```

### SSE Recovery

- A client subscribes with `after=<last_seen_seq>`.
- The server first emits journal events with a higher sequence number.
- The server then subscribes the client to new events.
- Slow clients do not block the agent loop. The server may disconnect a slow
  subscriber, which can reconnect and recover from the journal.

## State Model

### Session

```text
active -> archived
```

A session is independent of any client connection. Closing the TUI does not
close a session.

### Run

```text
queued
  -> running
  -> waiting_approval
  -> running
  -> completed

running          -> failed
running          -> cancelled
running          -> interrupted
waiting_approval -> cancelled
```

On server startup:

- Persisted `running` runs become `interrupted`.
- Persisted `waiting_approval` runs remain pending and may still be resolved.
  When they are resolved after a restart, the runtime rebuilds the agent
  history from the journal, injects the approved tool result or denial, and
  resumes the loop as a new in-process continuation of the same run.
- The first release does not automatically resume active LLM requests or shell
  processes after a restart.

The distinction is deliberate: session recovery is complete, while durable
background execution remains a later feature.

## SQLite Schema

The first release requires these tables:

```text
sessions
  id
  title
  last_seq
  created_at
  updated_at
  archived_at

runs
  id
  session_id
  status
  user_message
  started_at
  finished_at
  error

approvals
  id
  run_id
  tool_name
  arguments_json
  tool_call_id
  status
  created_at
  resolved_at
```

The database is stored under `~/.merak/`. Session journals are stored at:

```text
~/.merak/sessions/{session_id}.jsonl
```

## Agent Loop Integration

The server initializes providers, built-in tools, MCP clients, memory,
compaction, and the agent loop.

`AgentLoop` emits runtime events directly through an event sink:

- Text stream chunks emit `text_delta`.
- State transitions emit `state_changed`.
- Tool lifecycle emits `tool_started` and `tool_completed`.
- Tools requiring approval call `ApprovalGate`, producing
  `approval_requested` and suspending execution.
- Cancellation is checked before and after LLM calls, before and after each
  tool execution, and while waiting for approval.
- A post-restart approval resolution reconstructs the session history from
  journaled messages and tool events. An allowed request executes the persisted
  pending tool call; a denied request creates an error tool result. The runtime
  appends the result and resumes the same run. It does not attempt to recover
  an in-flight process stack.

Shell cancellation must terminate the child process. Network calls that cannot
be interrupted immediately discard their returned result if cancellation was
requested.

## Error Handling

HTTP errors are structured:

```json
{
  "error": {
    "code": "run_not_found",
    "message": "Run does not exist",
    "retryable": false
  }
}
```

Required behavior:

- Server unavailable: TUI shows a connection error and does not auto-start it.
- SSE disconnect: TUI reconnects using its last sequence number.
- LLM failure: persist and emit `run_failed`.
- Tool failure: emit `tool_completed` with `is_error=true`; the agent loop may
  decide how to proceed.
- Journal append failure: stop the run. Never broadcast an event that was not
  persisted.
- SQLite update failure: stop the run and log the storage error.
- Duplicate approval resolution: return the existing resolution.
- Concurrent run submission in one session: return `session_busy`.

## Testing

### `merak-storage`

- SQLite CRUD for sessions, runs, and approvals.
- JSONL append and sequence restoration.
- Ignore or repair an incomplete trailing JSONL line.
- Convert persisted `running` runs to `interrupted` on startup.
- Preserve `waiting_approval` runs on startup.

### `merak-runtime`

- Run a complete event sequence with fake LLM and fake tools.
- Suspend for approval, then allow and continue.
- Suspend for approval, then deny and continue with an error tool result.
- Cancel while waiting for approval.
- Cancel before and after an LLM call.
- Reject concurrent runs in one session.
- Replay missing events after an SSE-style reconnect boundary.

### `merak-http`

- Route contract tests.
- SSE frame encoding.
- Structured error mapping.
- Approval idempotency.
- Session event catch-up via `after`.

### CLI

- Report server connection failure.
- Create and restore sessions.
- Render recovered history before subscribing to live events.
- Post approval decisions.
- Cancel an active run.
- Verify that the REPL path no longer exists.

## Completion Criteria

The migration is complete only when:

```text
merak serve
merak tui
```

provide a working end-to-end experience:

- TUI creates or restores a session.
- TUI submits a user message.
- Server runs the real agent loop.
- TUI receives text, state, and tool events through SSE.
- Approval-required tools suspend until the TUI resolves the request.
- A run can be cancelled.
- SSE reconnection catches up through journal sequence numbers.
- Server restart preserves session history and marks unfinished execution as
  `interrupted`.
- The old REPL has been deleted.
- No TUI code owns or initializes agent execution dependencies.

## Future Extension

A Web UI can call the same API and consume the same SSE stream. Later work may
add authentication, multi-user isolation, remote edge tools, durable background
runs, and cloud deployment without moving the agent loop back into a client.
