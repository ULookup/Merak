import { describe, it, expect } from 'vitest';
import { reducer, initialState, type AppState } from '../AppState';

function state(overrides: Partial<AppState> = {}): AppState {
  return { ...initialState, sessionId: 'test-session', ...overrides };
}

describe('AppState reducer', () => {
  it('SET_SESSION resets messages and sets sessionId', () => {
    const prev = state({ messages: [{ id: 'm1', kind: 'user', text: 'hi' }], lastSeq: 5 });
    const next = reducer(prev, { type: 'SET_SESSION', sessionId: 'new-id' });
    expect(next.sessionId).toBe('new-id');
    expect(next.messages).toHaveLength(0);
    expect(next.lastSeq).toBe(0);
    expect(next.currentRun).toBeNull();
    expect(next.status).toBe('idle');
  });

  it('SET_METADATA stores metadata and sets selectedModel', () => {
    const prev = state();
    const next = reducer(prev, {
      type: 'SET_METADATA',
      metadata: {
        provider: 'openai', model: 'gpt-4o', models: [],
        permission_mode: 'default', memory: { enabled: false },
        tools: [], mcp_servers: [], agents: [], delegation_patterns: [],
      },
    });
    expect(next.metadata?.model).toBe('gpt-4o');
    expect(next.selectedModel).toBe('gpt-4o');
  });

  it('SET_SESSIONS replaces session list', () => {
    const prev = state();
    const next = reducer(prev, {
      type: 'SET_SESSIONS',
      sessions: [{ id: 's1', title: 'Test', last_seq: 0, created_at: '', updated_at: '', archived_at: null }],
    });
    expect(next.sessions).toHaveLength(1);
    expect(next.sessions[0].id).toBe('s1');
  });

  it('UPDATE_ASSISTANT appends to existing assistant message', () => {
    const prev = state({
      messages: [{ id: 'm1', kind: 'assistant', text: 'Hello', toolCallId: 'active' }],
    });
    const next = reducer(prev, { type: 'UPDATE_ASSISTANT', text: ' world' });
    expect(next.messages).toHaveLength(1);
    expect(next.messages[0].text).toBe('Hello world');
  });

  it('UPDATE_ASSISTANT creates new assistant when no active exists', () => {
    const prev = state();
    const next = reducer(prev, { type: 'UPDATE_ASSISTANT', text: 'Hi' });
    expect(next.messages).toHaveLength(1);
    expect(next.messages[0].kind).toBe('assistant');
    expect(next.messages[0].text).toBe('Hi');
    expect(next.messages[0].toolCallId).toBe('active');
  });

  it('SET_TOOL_RUNNING adds tool message', () => {
    const prev = state();
    const next = reducer(prev, {
      type: 'SET_TOOL_RUNNING',
      toolCallId: 'tc1', name: 'read_file', args: '{"path":"/x"}',
    });
    expect(next.messages).toHaveLength(1);
    const toolMsg = next.messages[0];
    expect(toolMsg.kind).toBe('tool');
    expect(toolMsg.toolCallId).toBe('tc1');
    expect(toolMsg.toolName).toBe('read_file');
    expect(toolMsg.toolRunning).toBe(true);
  });

  it('SET_TOOL_DONE updates matching tool message', () => {
    const prev = state({
      messages: [
        { id: 'm1', kind: 'tool', toolCallId: 'tc1', toolName: 'read_file', toolRunning: true },
      ],
    });
    const next = reducer(prev, {
      type: 'SET_TOOL_DONE',
      toolCallId: 'tc1', output: 'file contents', isError: false,
    });
    expect(next.messages).toHaveLength(1);
    expect(next.messages[0].toolRunning).toBe(false);
    expect(next.messages[0].toolOutput).toBe('file contents');
    expect(next.messages[0].toolIsError).toBe(false);
  });

  it('SET_APPROVAL adds approval message and sets waiting status', () => {
    const prev = state();
    const next = reducer(prev, {
      type: 'SET_APPROVAL',
      approvalId: 'a1', name: 'bash', args: 'rm -rf /',
    });
    expect(next.status).toBe('waiting_approval');
    expect(next.messages).toHaveLength(1);
    expect(next.messages[0].kind).toBe('approval');
  });

  it('CLEAR_APPROVAL returns to idle', () => {
    const prev = state({ status: 'waiting_approval' });
    const next = reducer(prev, { type: 'CLEAR_APPROVAL' });
    expect(next.status).toBe('idle');
  });

  it('SET_STATUS adds status pill', () => {
    const prev = state();
    const next = reducer(prev, { type: 'SET_STATUS', status: 'thinking' });
    expect(next.status).toBe('thinking');
    expect(next.messages).toHaveLength(1);
    expect(next.messages[0].kind).toBe('status_pill');
  });

  it('SET_USAGE accumulates tokens', () => {
    const prev = state({ usage: { inputTokens: 100, outputTokens: 50 } });
    const next = reducer(prev, { type: 'SET_USAGE', inputTokens: 30, outputTokens: 20 });
    expect(next.usage.inputTokens).toBe(130);
    expect(next.usage.outputTokens).toBe(70);
  });

  it('SET_CURRENT_RUN sets runId', () => {
    const prev = state();
    const next = reducer(prev, { type: 'SET_CURRENT_RUN', runId: 'run-1' });
    expect(next.currentRun).toBe('run-1');
  });

  it('COMMIT_ACTIVE removes active toolCallId markers', () => {
    const prev = state({
      messages: [
        { id: 'm1', kind: 'assistant', text: 'Hello', toolCallId: 'active' },
        { id: 'm2', kind: 'user', text: 'hi' },
      ],
    });
    const next = reducer(prev, { type: 'COMMIT_ACTIVE' });
    expect(next.messages[0].toolCallId).toBeUndefined();
    expect(next.messages[1].toolCallId).toBeUndefined();
  });

  it('SET_LAST_SEQ only moves forward', () => {
    const prev = state({ lastSeq: 5 });
    const next = reducer(prev, { type: 'SET_LAST_SEQ', seq: 3 });
    expect(next.lastSeq).toBe(5);
    const next2 = reducer(prev, { type: 'SET_LAST_SEQ', seq: 7 });
    expect(next2.lastSeq).toBe(7);
  });

  describe('SSE frame: run_started', () => {
    it('creates user message from payload.message', () => {
      const prev = state();
      const frame = {
        seq: 1, type: 'run_started',
        payload: { run_id: 'r1', message: 'Hello world' },
      };
      const next = reducer(prev, { type: 'APPLY_SSE', frame });
      const userMsgs = next.messages.filter((m) => m.kind === 'user');
      expect(userMsgs).toHaveLength(1);
      expect(userMsgs[0].text).toBe('Hello world');
    });

    it('sets currentRun', () => {
      const prev = state();
      const frame = {
        seq: 1, type: 'run_started',
        payload: { run_id: 'r99', message: 'hi' },
      };
      const next = reducer(prev, { type: 'APPLY_SSE', frame });
      expect(next.currentRun).toBe('r99');
    });
  });

  describe('SSE frame: run_completed', () => {
    it('commits active and clears run', () => {
      const prev = state({
        currentRun: 'r1',
        messages: [{ id: 'm1', kind: 'assistant', text: 'Done', toolCallId: 'active' }],
      });
      const frame = { seq: 2, type: 'run_completed', payload: {} };
      const next = reducer(prev, { type: 'APPLY_SSE', frame });
      expect(next.currentRun).toBeNull();
      expect(next.messages[0].toolCallId).toBeUndefined();
    });
  });

  describe('SSE frame: run_failed', () => {
    it('commits active and adds error message', () => {
      const prev = state({ currentRun: 'r1' });
      const frame = {
        seq: 2, type: 'run_failed',
        payload: { error: 'Something broke' },
      };
      const next = reducer(prev, { type: 'APPLY_SSE', frame });
      expect(next.messages).toHaveLength(1);
      expect(next.messages[0].kind).toBe('system');
      expect(next.messages[0].text).toBe('Something broke');
      expect(next.messages[0].error).toBe(true);
    });
  });
});
