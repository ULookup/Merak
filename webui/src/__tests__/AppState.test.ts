import { describe, expect, it } from 'vitest';
import { shouldReportWorldbuildingPartialFailure, shouldWarnBeforeClose } from '../App';
import { initialState, reducer, type AppState } from '../AppState';

function state(overrides: Partial<AppState> = {}): AppState {
  return { ...initialState, sessionId: 'test-session', ...overrides };
}

describe('AppState reducer', () => {
  it('SET_SESSION resets messages and sets sessionId', () => {
    const prev = state({
      messages: [{ id: 'm1', kind: 'user', text: 'hi' }],
      lastSeq: 5,
      pendingAsk: {
        runId: 'old-run',
        callId: 'old-call',
        question: 'Old question',
        multiSelect: false,
      },
      pendingCreation: { id: 'old-creation', runId: 'old-run', toolName: 'create_scene' },
    });
    const next = reducer(prev, { type: 'SET_SESSION', sessionId: 'new-id' });
    expect(next.sessionId).toBe('new-id');
    expect(next.messages).toHaveLength(0);
    expect(next.lastSeq).toBe(0);
    expect(next.currentRun).toBeNull();
    expect(next.status).toBe('idle');
    expect(next.pendingAsk).toBeNull();
    expect(next.pendingCreation).toBeNull();
  });

  it('SET_METADATA stores metadata and sets selectedModel', () => {
    const prev = state();
    const next = reducer(prev, {
      type: 'SET_METADATA',
      metadata: {
        provider: 'openai',
        model: 'gpt-4o',
        models: [],
        permission_mode: 'default',
        memory: { enabled: false },
        tools: [],
        mcp_servers: [],
        agents: [],
        delegation_patterns: [],
      },
    });
    expect(next.metadata?.model).toBe('gpt-4o');
    expect(next.selectedModel).toBe('gpt-4o');
  });

  it('SET_SESSIONS replaces session list', () => {
    const prev = state();
    const next = reducer(prev, {
      type: 'SET_SESSIONS',
      sessions: [
        {
          id: 's1',
          title: 'Test',
          world_id: null,
          agent_id: null,
          last_seq: 0,
          created_at: '',
          updated_at: '',
          archived_at: null,
        },
      ],
    });
    expect(next.sessions).toHaveLength(1);
    expect(next.sessions[0].id).toBe('s1');
  });

  it('stores worldbuilding data and resets detail panes when the world changes', () => {
    const prev = state({
      worldId: 'old_world',
      worlds: [{ id: 'old_world', name: 'Old World', description: '', created_at: '' }],
      agents: [{ id: 'a1', name: 'agent_lian', display_name: 'Lian', kind: 'character' }],
      foreshadowing: [{ id: 'f1', content: 'A silver key', status: 'open' }],
      secrets: [{ id: 's1', title: 'Hidden heir', status: 'sealed' }],
      worldTime: 'Third moon',
      worldbuildingStatus: 'ready',
    });

    const selected = reducer(prev, { type: 'SET_WORLD', worldId: 'new_world' });
    expect(selected.worldId).toBe('new_world');
    expect(selected.agents).toHaveLength(0);
    expect(selected.foreshadowing).toHaveLength(0);
    expect(selected.secrets).toHaveLength(0);
    expect(selected.worldTime).toBeNull();

    const next = reducer(selected, {
      type: 'SET_WORLDBUILDING_DATA',
      worlds: [{ id: 'new_world', name: 'New World', description: 'Draft realm', created_at: '' }],
      agents: [{ id: 'a2', name: 'agent_mira', display_name: 'Mira', kind: 'manager' }],
      foreshadowing: [{ id: 'f2', content: 'The clock stops at dusk', status: 'open' }],
      secrets: [{ id: 's2', title: 'The queen remembers', status: 'secret' }],
      worldTime: 'Dusk, day 4',
    });

    expect(next.worlds[0].name).toBe('New World');
    expect(next.agents[0].display_name).toBe('Mira');
    expect(next.foreshadowing[0].content).toContain('clock');
    expect(next.secrets[0].title).toContain('queen');
    expect(next.worldTime).toBe('Dusk, day 4');
    expect(next.worldbuildingStatus).toBe('ready');
  });

  it('tracks the selected inspector tab', () => {
    const next = reducer(state(), { type: 'SET_INSPECTOR_TAB', tab: 'agents' });
    expect(next.inspectorTab).toBe('agents');
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
      toolCallId: 'tc1',
      name: 'read_file',
      args: '{"path":"/x"}',
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
      toolCallId: 'tc1',
      output: 'file contents',
      isError: false,
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
      approvalId: 'a1',
      name: 'bash',
      args: 'rm -rf /',
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

  it('SET_STATUS updates live run status without adding timeline messages', () => {
    const prev = state();
    const next = reducer(prev, { type: 'SET_STATUS', status: 'thinking' });
    expect(next.status).toBe('thinking');
    expect(next.messages).toHaveLength(0);
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

  it('tracks and resolves interactive SSE requests', () => {
    const askFrame = {
      seq: 10,
      type: 'ask_user_requested',
      payload: {
        run_id: 'r1',
        call_id: 'call_1',
        question: 'Choose POV',
        options: ['First person', 'Third person'],
      },
    };
    const asked = reducer(state(), { type: 'APPLY_SSE', frame: askFrame });
    expect(asked.pendingAsk).toEqual({
      runId: 'r1',
      callId: 'call_1',
      question: 'Choose POV',
      choices: ['First person', 'Third person'],
      multiSelect: false,
    });
    expect(reducer(asked, { type: 'RESOLVE_ASK', callId: 'call_1' }).pendingAsk).toBeNull();

    const creationFrame = {
      seq: 11,
      type: 'creation_requested',
      payload: {
        run_id: 'r1',
        creation_id: 'creation_1',
        tool: 'create_scene',
        preview: { title: 'Arrival' },
      },
    };
    const requested = reducer(asked, { type: 'APPLY_SSE', frame: creationFrame });
    expect(requested.pendingCreation).toEqual({
      id: 'creation_1',
      runId: 'r1',
      toolName: 'create_scene',
      preview: { title: 'Arrival' },
    });
    expect(
      reducer(requested, {
        type: 'APPLY_SSE',
        frame: {
          seq: 12,
          type: 'creation_resolved',
          payload: { creation_id: 'creation_1', decision: 'allow' },
        },
      }).pendingCreation,
    ).toBeNull();
    expect(
      reducer(requested, { type: 'RESOLVE_CREATION', creationId: 'creation_1' }).pendingCreation,
    ).toBeNull();
  });

  it('retains a creation request when its resolving SSE result fails', () => {
    const prev = state({
      pendingCreation: { id: 'creation_1', runId: 'run_1', toolName: 'create_scene' },
      lastSeq: 4,
    });
    const next = reducer(prev, {
      type: 'APPLY_SSE',
      frame: {
        seq: 5,
        type: 'creation_resolved',
        payload: { creation_id: 'creation_1', result: { ok: false } },
      },
    });
    expect(next.pendingCreation).toEqual(prev.pendingCreation);
  });

  it('does not let stale local resolutions clear newer requests', () => {
    const prev = state({
      pendingAsk: {
        runId: 'run_2',
        callId: 'call_2',
        question: 'New question',
        multiSelect: false,
      },
      pendingCreation: { id: 'creation_2', runId: 'run_2', toolName: 'create_scene' },
    });
    expect(reducer(prev, { type: 'RESOLVE_ASK', callId: 'call_1' }).pendingAsk).toEqual(
      prev.pendingAsk,
    );
    expect(
      reducer(prev, { type: 'RESOLVE_CREATION', creationId: 'creation_1' }).pendingCreation,
    ).toEqual(prev.pendingCreation);
  });

  it.each(['run_completed', 'run_failed', 'run_cancelled', 'run_interrupted'])(
    'clears matching pending requests on %s',
    (type) => {
      const prev = state({
        currentRun: 'run_1',
        pendingAsk: {
          runId: 'run_1',
          callId: 'call_1',
          question: 'Question',
          multiSelect: false,
        },
        pendingCreation: { id: 'creation_1', runId: 'run_1', toolName: 'create_scene' },
      });
      const next = reducer(prev, {
        type: 'APPLY_SSE',
        frame: { seq: 8, type, payload: { run_id: 'run_1' } },
      });
      expect(next.pendingAsk).toBeNull();
      expect(next.pendingCreation).toBeNull();
    },
  );

  it('keeps newer pending requests when an older run terminates', () => {
    const prev = state({
      currentRun: 'run_2',
      pendingAsk: {
        runId: 'run_2',
        callId: 'call_2',
        question: 'Question',
        multiSelect: false,
      },
      pendingCreation: { id: 'creation_2', runId: 'run_2', toolName: 'create_scene' },
    });
    const next = reducer(prev, {
      type: 'APPLY_SSE',
      frame: { seq: 8, type: 'run_completed', payload: { run_id: 'run_1' } },
    });
    expect(next.pendingAsk).toEqual(prev.pendingAsk);
    expect(next.pendingCreation).toEqual(prev.pendingCreation);
  });

  it('retains a creation from another run even when currentRun matches the terminal event', () => {
    const pendingCreation = { id: 'creation_2', runId: 'run_2', toolName: 'create_scene' };
    const next = reducer(state({ currentRun: 'run_1', pendingCreation }), {
      type: 'APPLY_SSE',
      frame: { seq: 8, type: 'run_completed', payload: { run_id: 'run_1' } },
    });
    expect(next.pendingCreation).toEqual(pendingCreation);
  });

  it('clears a creation matching the terminal run even when currentRun has moved on', () => {
    const next = reducer(
      state({
        currentRun: 'run_3',
        pendingCreation: { id: 'creation_2', runId: 'run_2', toolName: 'create_scene' },
      }),
      {
        type: 'APPLY_SSE',
        frame: { seq: 8, type: 'run_failed', payload: { run_id: 'run_2', error: 'stopped' } },
      },
    );
    expect(next.pendingCreation).toBeNull();
  });

  it('ignores duplicate and stale nonzero SSE frames before applying their effects', () => {
    const askFrame = {
      seq: 10,
      type: 'ask_user_requested',
      payload: { run_id: 'r1', question: 'Choose POV' },
    };
    const prev = state({ lastSeq: 10 });
    expect(reducer(prev, { type: 'APPLY_SSE', frame: askFrame })).toEqual(prev);

    const zeroSeq = reducer(prev, {
      type: 'APPLY_SSE',
      frame: { ...askFrame, seq: 0 },
    });
    expect(zeroSeq.pendingAsk).toEqual({
      runId: 'r1',
      callId: '',
      question: 'Choose POV',
      multiSelect: false,
    });
    expect(zeroSeq.lastSeq).toBe(10);
  });

  describe('SSE frame: run_started', () => {
    it('creates user message from payload.message', () => {
      const prev = state();
      const frame = {
        seq: 1,
        type: 'run_started',
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
        seq: 1,
        type: 'run_started',
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
        status: 'responding',
        messages: [{ id: 'm1', kind: 'assistant', text: 'Done', toolCallId: 'active' }],
      });
      const frame = { seq: 2, type: 'run_completed', payload: {} };
      const next = reducer(prev, { type: 'APPLY_SSE', frame });
      expect(next.currentRun).toBeNull();
      expect(next.status).toBe('idle');
      expect(next.messages[0].toolCallId).toBeUndefined();
    });

    it('keeps generated writing on disk instead of capturing assistant text as a draft', () => {
      const prev = state({
        currentRun: 'r1',
        inspectorTab: 'story',
        messages: [
          {
            id: 'm1',
            kind: 'assistant',
            text: 'The Bridge at Dusk\n\nKaelen crossed under a copper sky.',
            toolCallId: 'active',
          },
        ],
      });
      const frame = { seq: 2, type: 'run_completed', payload: {} };
      const next = reducer(prev, { type: 'APPLY_SSE', frame });
      expect(next.generatedFiles).toHaveLength(0);
      expect(next.inspectorTab).toBe('story');
      expect(next.messages[0].toolCallId).toBeUndefined();
    });
  });

  it('tracks generated file entries and output directory', () => {
    const withDir = reducer(state(), {
      type: 'SET_OUTPUT_DIRECTORY',
      path: '/Users/me/novel',
    });
    const next = reducer(withDir, {
      type: 'REGISTER_GENERATED_FILE',
      file: {
        id: 'file_1',
        title: 'chapter-12',
        path: '/Users/me/novel/chapter-12.md',
        updatedAt: 1,
      },
    });
    expect(next.outputDirectory).toBe('/Users/me/novel');
    expect(next.generatedFiles[0].path).toBe('/Users/me/novel/chapter-12.md');
    expect(next.inspectorTab).toBe('files');
  });

  it('detects generated file paths from successful tool output', () => {
    const prev = state({
      messages: [
        { id: 'm1', kind: 'tool', toolCallId: 'tc1', toolName: 'write_file', toolRunning: true },
      ],
    });
    const next = reducer(prev, {
      type: 'SET_TOOL_DONE',
      toolCallId: 'tc1',
      output: 'Wrote /Users/me/novel/chapter-12.md',
      isError: false,
    });
    expect(next.generatedFiles[0].path).toBe('/Users/me/novel/chapter-12.md');
    expect(next.outputDirectory).toBe('/Users/me/novel');
    expect(next.inspectorTab).toBe('files');
  });

  it('opens generated files into an editable buffer', () => {
    const prev = state({
      generatedFiles: [
        {
          id: 'file_1',
          title: 'chapter-12',
          path: '/Users/me/novel/chapter-12.md',
          updatedAt: 1,
        },
      ],
    });
    const opened = reducer(prev, { type: 'OPEN_GENERATED_FILE', fileId: 'file_1' });
    expect(opened.activeEditorFileId).toBe('file_1');
    expect(opened.editorBuffers.file_1).toBe('');

    const edited = reducer(opened, {
      type: 'UPDATE_EDITOR_BUFFER',
      fileId: 'file_1',
      content: 'The revised chapter begins here.',
    });
    expect(edited.editorBuffers.file_1).toBe('The revised chapter begins here.');
  });

  describe('SSE frame: run_failed', () => {
    it('commits active, clears run, returns idle, and adds error message', () => {
      const prev = state({ currentRun: 'r1', status: 'acting' });
      const frame = {
        seq: 2,
        type: 'run_failed',
        payload: { error: 'Something broke' },
      };
      const next = reducer(prev, { type: 'APPLY_SSE', frame });
      expect(next.currentRun).toBeNull();
      expect(next.status).toBe('idle');
      expect(next.messages).toHaveLength(1);
      expect(next.messages[0].kind).toBe('system');
      expect(next.messages[0].text).toBe('Something broke');
      expect(next.messages[0].error).toBe(true);
    });
  });
});

describe('worldbuilding bootstrap error policy', () => {
  it('does not report secondary endpoint failures when an overview is available', () => {
    expect(shouldReportWorldbuildingPartialFailure(true, true)).toBe(false);
  });

  it('reports secondary endpoint failures when no overview is available', () => {
    expect(shouldReportWorldbuildingPartialFailure(false, true)).toBe(true);
  });
});

describe('close protection policy', () => {
  it('warns before closing while a run is active', () => {
    expect(shouldWarnBeforeClose(state({ currentRun: 'run_1', status: 'thinking' }))).toBe(true);
  });

  it('warns before closing when editor changes are not safely persisted', () => {
    expect(shouldWarnBeforeClose(state({ editorSaveStatus: 'dirty' }))).toBe(true);
    expect(shouldWarnBeforeClose(state({ editorSaveStatus: 'saving' }))).toBe(true);
  });

  it('does not warn when the workbench is idle and saved', () => {
    expect(shouldWarnBeforeClose(state({ currentRun: null, status: 'idle' }))).toBe(false);
  });
});
