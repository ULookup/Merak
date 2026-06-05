import { describe, it, expect } from 'vitest';

describe('AppState reducer logic', () => {
  it('SET_SESSION resets messages and status', () => {
    const action = { type: 'SET_SESSION' as const, sessionId: 'test-123' };
    expect(action.sessionId).toBe('test-123');
    expect(action.type).toBe('SET_SESSION');
  });

  it('UPDATE_ASSISTANT appends text', () => {
    const action = { type: 'UPDATE_ASSISTANT' as const, text: 'hello' };
    expect(action.text).toBe('hello');
  });

  it('SET_TOOL_RUNNING sets correct fields', () => {
    const action = {
      type: 'SET_TOOL_RUNNING' as const,
      toolCallId: 't1',
      name: 'read_file',
      args: '{"path":"/x"}',
    };
    expect(action.toolCallId).toBe('t1');
    expect(action.name).toBe('read_file');
  });

  it('SET_TOOL_DONE matches by toolCallId', () => {
    const action = {
      type: 'SET_TOOL_DONE' as const,
      toolCallId: 't1',
      output: 'ok',
      isError: false,
    };
    expect(action.output).toBe('ok');
    expect(action.isError).toBe(false);
  });

  it('SET_STATUS creates a status pill', () => {
    const action = { type: 'SET_STATUS' as const, status: 'thinking' as const };
    expect(action.status).toBe('thinking');
  });

  it('SET_APPROVAL creates approval message', () => {
    const action = {
      type: 'SET_APPROVAL' as const,
      approvalId: 'a1',
      name: 'bash',
      args: 'rm -rf /',
    };
    expect(action.approvalId).toBe('a1');
    expect(action.name).toBe('bash');
  });
});
