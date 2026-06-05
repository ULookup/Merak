import { createContext, useContext, useReducer, type Dispatch, type ReactNode } from 'react';
import type { Message, RuntimeMetadata, SessionSummary, SseFrame, StatusLabel } from './api/types';

export interface AppState {
  sessionId: string;
  lastSeq: number;
  currentRun: string | null;
  messages: Message[];
  status: StatusLabel;
  usage: { inputTokens: number; outputTokens: number };
  metadata: RuntimeMetadata | null;
  sessions: SessionSummary[];
  worldId: string | null;
  selectedModel: string;
}

export const initialState: AppState = {
  sessionId: '',
  lastSeq: 0,
  currentRun: null,
  messages: [],
  status: 'idle',
  usage: { inputTokens: 0, outputTokens: 0 },
  metadata: null,
  sessions: [],
  worldId: null,
  selectedModel: '',
};

let nextId = 1;
function msgId(): string {
  return `msg_${nextId++}_${Date.now()}`;
}

export type Action =
  | { type: 'SET_SESSION'; sessionId: string }
  | { type: 'SET_METADATA'; metadata: RuntimeMetadata }
  | { type: 'SET_SESSIONS'; sessions: SessionSummary[] }
  | { type: 'SET_WORLD'; worldId: string | null }
  | { type: 'SET_MODEL'; model: string }
  | { type: 'SET_LAST_SEQ'; seq: number }
  | { type: 'APPEND_MESSAGE'; message: Message }
  | { type: 'UPDATE_ASSISTANT'; text: string }
  | { type: 'SET_TOOL_RUNNING'; toolCallId: string; name: string; args: string }
  | { type: 'SET_TOOL_DONE'; toolCallId: string; output: string; isError: boolean }
  | { type: 'SET_APPROVAL'; approvalId: string; name: string; args: string }
  | { type: 'CLEAR_APPROVAL' }
  | { type: 'SET_STATUS'; status: StatusLabel }
  | { type: 'SET_USAGE'; inputTokens: number; outputTokens: number }
  | { type: 'SET_CURRENT_RUN'; runId: string | null }
  | { type: 'COMMIT_ACTIVE' }
  | { type: 'APPLY_SSE'; frame: SseFrame };

export function reducer(state: AppState, action: Action): AppState {
  switch (action.type) {
    case 'SET_SESSION':
      return {
        ...state,
        sessionId: action.sessionId,
        messages: [],
        lastSeq: 0,
        currentRun: null,
        status: 'idle',
      };

    case 'SET_METADATA':
      return { ...state, metadata: action.metadata, selectedModel: action.metadata.model };

    case 'SET_SESSIONS':
      return { ...state, sessions: action.sessions };

    case 'SET_WORLD':
      return { ...state, worldId: action.worldId };

    case 'SET_MODEL':
      return { ...state, selectedModel: action.model };

    case 'SET_LAST_SEQ':
      return { ...state, lastSeq: Math.max(state.lastSeq, action.seq) };

    case 'APPEND_MESSAGE':
      return { ...state, messages: [...state.messages, action.message] };

    case 'UPDATE_ASSISTANT': {
      const msgs = [...state.messages];
      const last = msgs[msgs.length - 1];
      if (last && last.kind === 'assistant' && last.toolCallId === 'active') {
        msgs[msgs.length - 1] = { ...last, text: (last.text ?? '') + action.text };
      } else {
        msgs.push({ id: msgId(), kind: 'assistant', text: action.text, toolCallId: 'active' });
      }
      return { ...state, messages: msgs };
    }

    case 'SET_TOOL_RUNNING': {
      const msgs = [...state.messages];
      msgs.push({
        id: msgId(),
        kind: 'tool',
        toolCallId: action.toolCallId,
        toolName: action.name,
        toolArgs: action.args,
        toolRunning: true,
      });
      return { ...state, messages: msgs };
    }

    case 'SET_TOOL_DONE': {
      const msgs = state.messages.map((m) =>
        m.kind === 'tool' && m.toolCallId === action.toolCallId
          ? { ...m, toolRunning: false, toolOutput: action.output, toolIsError: action.isError }
          : m,
      );
      return { ...state, messages: msgs };
    }

    case 'SET_APPROVAL':
      return {
        ...state,
        status: 'waiting_approval',
        messages: [
          ...state.messages,
          {
            id: msgId(),
            kind: 'approval',
            approvalId: action.approvalId,
            approvalName: action.name,
            approvalArgs: action.args,
          },
        ],
      };

    case 'CLEAR_APPROVAL':
      return { ...state, status: 'idle' };

    case 'SET_STATUS':
      return {
        ...state,
        status: action.status,
        messages: [
          ...state.messages,
          { id: msgId(), kind: 'status_pill', statusLabel: action.status },
        ],
      };

    case 'SET_USAGE':
      return {
        ...state,
        usage: {
          inputTokens: state.usage.inputTokens + action.inputTokens,
          outputTokens: state.usage.outputTokens + action.outputTokens,
        },
      };

    case 'SET_CURRENT_RUN':
      return { ...state, currentRun: action.runId };

    case 'COMMIT_ACTIVE': {
      const msgs = state.messages.map((m) =>
        m.toolCallId === 'active' ? { ...m, toolCallId: undefined } : m,
      );
      return { ...state, messages: msgs };
    }

    case 'APPLY_SSE': {
      const { frame } = action;
      if (frame.seq > 0) state = { ...state, lastSeq: Math.max(state.lastSeq, frame.seq) };
      return applySseFrame(state, frame);
    }

    default:
      return state;
  }
}

function applySseFrame(state: AppState, frame: SseFrame): AppState {
  const { type, payload } = frame;
  const p = (payload ?? {}) as Record<string, unknown>;

  switch (type) {
    case 'run_started':
      return reducer(
        reducer(state, { type: 'SET_CURRENT_RUN', runId: (p.run_id as string) ?? '' }),
        {
          type: 'APPEND_MESSAGE',
          message: { id: msgId(), kind: 'user', text: (p.message as string) ?? '' },
        },
      );

    case 'text_delta':
      return reducer(state, { type: 'UPDATE_ASSISTANT', text: (p.text as string) ?? '' });

    case 'state_changed':
      return reducer(state, { type: 'SET_STATUS', status: (p.to as StatusLabel) ?? 'idle' });

    case 'tool_started':
      return reducer(state, {
        type: 'SET_TOOL_RUNNING',
        toolCallId: (p.id ?? p.tool_call_id ?? '') as string,
        name: (p.name ?? p.tool ?? '') as string,
        args: (p.arguments ?? '') as string,
      });

    case 'tool_completed':
      return reducer(state, {
        type: 'SET_TOOL_DONE',
        toolCallId: (p.id ?? '') as string,
        output: (p.output ?? '') as string,
        isError: (p.is_error ?? false) as boolean,
      });

    case 'approval_requested':
      return reducer(state, {
        type: 'SET_APPROVAL',
        approvalId: (p.approval_id ?? '') as string,
        name: (p.name ?? p.tool ?? '') as string,
        args: (p.arguments ?? '') as string,
      });

    case 'approval_resolved':
      return reducer(state, { type: 'CLEAR_APPROVAL' });

    case 'usage_updated':
      return reducer(state, {
        type: 'SET_USAGE',
        inputTokens: (p.input_tokens ?? 0) as number,
        outputTokens: (p.output_tokens ?? 0) as number,
      });

    case 'delegation_started':
    case 'sub_run_started':
    case 'sub_run_completed':
    case 'delegation_completed':
      return reducer(state, {
        type: 'APPEND_MESSAGE',
        message: { id: msgId(), kind: 'system', text: `${type} — ${JSON.stringify(p)}` },
      });

    case 'run_completed':
    case 'run_failed':
    case 'run_cancelled':
    case 'run_interrupted':
      return reducer(
        reducer(state, { type: 'COMMIT_ACTIVE' }),
        type === 'run_failed'
          ? {
              type: 'APPEND_MESSAGE',
              message: {
                id: msgId(),
                kind: 'system',
                text: (p.error as string) ?? 'Run failed',
                error: true,
              },
            }
          : { type: 'SET_CURRENT_RUN', runId: null },
      );

    default:
      return state;
  }
}

const AppContext = createContext<{
  state: AppState;
  dispatch: Dispatch<Action>;
} | null>(null);

export function AppStateProvider({ children }: { children: ReactNode }) {
  const [state, dispatch] = useReducer(reducer, initialState);
  return <AppContext.Provider value={{ state, dispatch }}>{children}</AppContext.Provider>;
}

export function useAppState() {
  const ctx = useContext(AppContext);
  if (!ctx) throw new Error('useAppState must be used within AppStateProvider');
  return ctx;
}
