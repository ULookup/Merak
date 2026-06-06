import { createContext, useContext, useReducer, type Dispatch, type ReactNode } from 'react';
import type {
  ForeshadowingItem,
  Message,
  RuntimeMetadata,
  SecretItem,
  SessionSummary,
  SseFrame,
  StatusLabel,
  WorldAgent,
  WorldSummary,
} from './api/types';

export type InspectorTab = 'story' | 'files' | 'agents' | 'run';
export type WorldbuildingStatus = 'idle' | 'loading' | 'ready' | 'error';

export interface GeneratedFileEntry {
  id: string;
  title: string;
  path: string;
  updatedAt: number;
}

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
  inspectorTab: InspectorTab;
  worlds: WorldSummary[];
  agents: WorldAgent[];
  foreshadowing: ForeshadowingItem[];
  secrets: SecretItem[];
  worldTime: string | null;
  worldbuildingStatus: WorldbuildingStatus;
  worldbuildingError: string | null;
  outputDirectory: string | null;
  generatedFiles: GeneratedFileEntry[];
  activeEditorFileId: string | null;
  editorBuffers: Record<string, string>;
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
  inspectorTab: 'story',
  worlds: [],
  agents: [],
  foreshadowing: [],
  secrets: [],
  worldTime: null,
  worldbuildingStatus: 'idle',
  worldbuildingError: null,
  outputDirectory: null,
  generatedFiles: [],
  activeEditorFileId: null,
  editorBuffers: {},
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
  | { type: 'SET_INSPECTOR_TAB'; tab: InspectorTab }
  | { type: 'SET_WORLDS'; worlds: WorldSummary[] }
  | { type: 'SET_WORLDBUILDING_STATUS'; status: WorldbuildingStatus; error?: string | null }
  | {
      type: 'SET_WORLDBUILDING_DATA';
      worlds?: WorldSummary[];
      agents: WorldAgent[];
      foreshadowing: ForeshadowingItem[];
      secrets: SecretItem[];
      worldTime: string | null;
    }
  | { type: 'SET_OUTPUT_DIRECTORY'; path: string | null }
  | { type: 'REGISTER_GENERATED_FILE'; file: GeneratedFileEntry }
  | { type: 'OPEN_GENERATED_FILE'; fileId: string }
  | { type: 'UPDATE_EDITOR_BUFFER'; fileId: string; content: string }
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
      return {
        ...state,
        worldId: action.worldId,
        agents: [],
        foreshadowing: [],
        secrets: [],
        worldTime: null,
        worldbuildingStatus: action.worldId ? 'loading' : 'idle',
        worldbuildingError: null,
      };

    case 'SET_INSPECTOR_TAB':
      return { ...state, inspectorTab: action.tab };

    case 'SET_OUTPUT_DIRECTORY':
      return { ...state, outputDirectory: action.path };

    case 'REGISTER_GENERATED_FILE':
      return {
        ...state,
        generatedFiles: [
          action.file,
          ...state.generatedFiles.filter((file) => file.path !== action.file.path),
        ],
        outputDirectory:
          state.outputDirectory ?? directoryFromPath(action.file.path) ?? state.outputDirectory,
        inspectorTab: 'files',
      };

    case 'OPEN_GENERATED_FILE': {
      const file = state.generatedFiles.find((item) => item.id === action.fileId);
      if (!file) return state;
      return {
        ...state,
        activeEditorFileId: file.id,
        inspectorTab: 'files',
        editorBuffers: {
          ...state.editorBuffers,
          [file.id]: state.editorBuffers[file.id] ?? '',
        },
      };
    }

    case 'UPDATE_EDITOR_BUFFER':
      return {
        ...state,
        editorBuffers: {
          ...state.editorBuffers,
          [action.fileId]: action.content,
        },
      };

    case 'SET_TOOL_DONE': {
      const msgs = state.messages.map((m) =>
        m.kind === 'tool' && m.toolCallId === action.toolCallId
          ? { ...m, toolRunning: false, toolOutput: action.output, toolIsError: action.isError }
          : m,
      );
      const files = action.isError ? [] : generatedFilesFromText(action.output);
      return {
        ...state,
        messages: msgs,
        generatedFiles: [
          ...files,
          ...state.generatedFiles.filter(
            (file) => !files.some((newFile) => newFile.path === file.path),
          ),
        ],
        outputDirectory:
          state.outputDirectory ??
          files.map((file) => directoryFromPath(file.path)).find(Boolean) ??
          null,
        inspectorTab: files.length > 0 ? 'files' : state.inspectorTab,
      };
    }

    case 'SET_WORLDS':
      return { ...state, worlds: action.worlds };

    case 'SET_WORLDBUILDING_STATUS':
      return {
        ...state,
        worldbuildingStatus: action.status,
        worldbuildingError: action.error ?? null,
      };

    case 'SET_WORLDBUILDING_DATA':
      return {
        ...state,
        worlds: action.worlds ?? state.worlds,
        agents: action.agents,
        foreshadowing: action.foreshadowing,
        secrets: action.secrets,
        worldTime: action.worldTime,
        worldbuildingStatus: 'ready',
        worldbuildingError: null,
      };

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
      return { ...state, status: action.status };

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
  const outer = (payload ?? {}) as Record<string, unknown>;
  const inner = (outer.payload as Record<string, unknown>) ?? {};
  const p = { ...outer, ...inner } as Record<string, unknown>;

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

    case 'state_changed': {
      const to = ((p.to as string) ?? '').toLowerCase();
      // Terminal states have their own events (run_completed/run_failed)
      if (to === 'complete' || to === 'error') return state;
      const validLabels = new Set(['idle', 'thinking', 'responding', 'acting', 'observing']);
      const label = validLabels.has(to) ? (to as StatusLabel) : state.status;
      return reducer(state, { type: 'SET_STATUS', status: label });
    }

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
      return reducer(
        reducer(reducer(state, { type: 'COMMIT_ACTIVE' }), {
          type: 'SET_CURRENT_RUN',
          runId: null,
        }),
        { type: 'SET_STATUS', status: 'idle' },
      );

    case 'run_failed':
    case 'run_cancelled':
    case 'run_interrupted':
      state = reducer(reducer(state, { type: 'COMMIT_ACTIVE' }), {
        type: 'SET_CURRENT_RUN',
        runId: null,
      });
      state = reducer(state, { type: 'SET_STATUS', status: 'idle' });
      return type === 'run_failed'
        ? reducer(state, {
            type: 'APPEND_MESSAGE',
            message: {
              id: msgId(),
              kind: 'system',
              text: (p.error as string) ?? 'Run failed',
              error: true,
            },
          })
        : state;

    default:
      return state;
  }
}

function generatedFilesFromText(text: string): GeneratedFileEntry[] {
  const paths = new Set<string>();
  const filePattern =
    /(?:^|\s)(\/[^\s"'`]+?\.(?:md|markdown|txt|docx|json|ya?ml)|[A-Za-z0-9_.-]+(?:\/[A-Za-z0-9_.-]+)+\.(?:md|markdown|txt|docx|json|ya?ml))/gi;

  for (const match of text.matchAll(filePattern)) {
    const path = match[1]?.replace(/[),.;:]+$/, '');
    if (path) paths.add(path);
  }

  return [...paths].map((path) => ({
    id: `file_${path}`,
    title: fileTitle(path),
    path,
    updatedAt: Date.now(),
  }));
}

function directoryFromPath(path: string): string | null {
  const index = path.lastIndexOf('/');
  if (index <= 0) return null;
  return path.slice(0, index);
}

function fileTitle(path: string): string {
  return (
    path
      .split('/')
      .filter(Boolean)
      .pop()
      ?.replace(/\.(md|markdown|txt|docx|json|ya?ml)$/i, '')
      .replace(/[-_]+/g, ' ') || 'Generated file'
  );
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
