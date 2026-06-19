import { createContext, useContext, useReducer, type Dispatch, type ReactNode } from 'react';
import type {
  ChapterReviewItem,
  ConditionState,
  ForeshadowingItem,
  Message,
  PendingAsk,
  PendingCreation,
  PhaseTransition,
  PipelineViewData,
  RuntimeMetadata,
  SecretItem,
  SessionSummary,
  SseFrame,
  StatusLabel,
  StoryOverview,
  UiCapabilities,
  WorkspaceFile,
  WorkspaceFileContent,
  WorldAgent,
  WorldSummary,
} from './api/types';

export type InspectorTab = 'story' | 'files' | 'agents' | 'run' | 'creation';
export type AppPage = 'workbench' | 'settings' | 'editor';
export type WorldbuildingStatus = 'idle' | 'loading' | 'ready' | 'error';

export interface GeneratedFileEntry {
  id: string;
  title: string;
  path: string;
  updatedAt: number;
}

export interface RunTimelineItem {
  id: string;
  type: 'state' | 'tool' | 'approval' | 'file' | 'error';
  label: string;
  detail?: string;
  at: number;
  status?: StatusLabel | 'completed' | 'failed';
}

export interface AppState {
  appPhase: 'loading' | 'no_world' | 'no_agent' | 'ready';
  currentPage: AppPage;
  activeEditorChapterId: string | null;
  activeEditorChapterTitle: string;
  agentId: string | null;
  sessionId: string;
  lastSeq: number;
  currentRun: string | null;
  pendingAsk: PendingAsk | null;
  pendingCreation: PendingCreation | null;
  lastRunId: string | null;
  messages: Message[];
  status: StatusLabel;
  usage: { inputTokens: number; outputTokens: number };
  metadata: RuntimeMetadata | null;
  capabilities: UiCapabilities;
  fallback: {
    capabilities: boolean;
    storyOverview: boolean;
    workspaceFiles: boolean;
    editorSave: boolean;
  };
  sessions: SessionSummary[];
  worldId: string | null;
  selectedModel: string;
  inspectorTab: InspectorTab;
  worlds: WorldSummary[];
  agents: WorldAgent[];
  foreshadowing: ForeshadowingItem[];
  secrets: SecretItem[];
  worldTime: string | null;
  storyOverview: StoryOverview | null;
  worldbuildingStatus: WorldbuildingStatus;
  worldbuildingError: string | null;
  outputDirectory: string | null;
  generatedFiles: GeneratedFileEntry[];
  workspaceFiles: WorkspaceFile[];
  fileSearch: string;
  fileTypeFilter: string;
  activeEditorFileId: string | null;
  editorBuffers: Record<string, string>;
  editorOriginals: Record<string, string>;
  editorVersions: Record<string, string>;
  editorSaveStatus: 'idle' | 'dirty' | 'saving' | 'saved' | 'error';
  editorError: string | null;
  runTimeline: RunTimelineItem[];
  storyVersion: number;
  pipelinePhase: string | null;
  // ═══ Pipeline (extended) ═══
  pipelineConditions: ConditionState[];
  pipelineActiveWorkflow: string;
  pipelineAdvanceError?: string;
  pipelineHistory: PhaseTransition[];
  pipelineNextAllowed: string[];
  pipelineAllowedRetreat: string[];
  pipelineAutoAdvance: boolean;
  showPhaseAdvancePrompt: {
    phase: string;
    nextPhase: string;
    conditions: ConditionState[];
  } | null;
  pipelineCycleComplete: { message: string } | null;
  showSetupWizard: boolean;
  llmConfigured: boolean;
  chapterReview: ChapterReviewItem | null;
  showExportDialog: boolean;
  userPreferences: { default_genre: string; preferred_style: string } | null;
}

export const initialState: AppState = {
  appPhase: 'loading',
  currentPage: 'workbench',
  activeEditorChapterId: null,
  activeEditorChapterTitle: '',
  agentId: null,
  sessionId: '',
  lastSeq: 0,
  currentRun: null,
  pendingAsk: null,
  pendingCreation: null,
  lastRunId: null,
  messages: [],
  status: 'idle',
  usage: { inputTokens: 0, outputTokens: 0 },
  metadata: null,
  capabilities: {
    files: false,
    story_overview: false,
    session_archive: false,
    world_create: false,
    editor_save: false,
  },
  fallback: {
    capabilities: false,
    storyOverview: false,
    workspaceFiles: false,
    editorSave: false,
  },
  sessions: [],
  worldId: null,
  selectedModel: '',
  inspectorTab: 'story',
  worlds: [],
  agents: [],
  foreshadowing: [],
  secrets: [],
  worldTime: null,
  storyOverview: null,
  worldbuildingStatus: 'idle',
  worldbuildingError: null,
  outputDirectory: null,
  generatedFiles: [],
  workspaceFiles: [],
  fileSearch: '',
  fileTypeFilter: 'all',
  activeEditorFileId: null,
  storyVersion: 0,
  editorBuffers: {},
  editorOriginals: {},
  editorVersions: {},
  editorSaveStatus: 'idle',
  editorError: null,
  runTimeline: [],
  pipelinePhase: null,
  pipelineConditions: [],
  pipelineActiveWorkflow: '',
  pipelineHistory: [],
  pipelineNextAllowed: [],
  pipelineAllowedRetreat: [],
  pipelineAutoAdvance: true,
  showPhaseAdvancePrompt: null,
  pipelineCycleComplete: null,
  showSetupWizard: false,
  llmConfigured: false,
  chapterReview: null,
  showExportDialog: false,
  userPreferences: null,
};

let nextId = 1;
function msgId(): string {
  return `msg_${nextId++}_${Date.now()}`;
}

function findActiveAssistantIndex(messages: Message[]) {
  for (let index = messages.length - 1; index >= 0; index--) {
    const msg = messages[index];
    if (msg.kind === 'assistant' && msg.toolCallId === 'active') return index;
  }
  return -1;
}

function ensureActiveAssistantMessage(messages: Message[], status: StatusLabel): Message[] {
  const now = Date.now();
  const index = findActiveAssistantIndex(messages);
  if (index >= 0) {
    return messages.map((msg, currentIndex) =>
      currentIndex === index
        ? {
            ...msg,
            assistantStatus: status,
            pending: (msg.text ?? '').trim().length === 0,
            thinkingStartedAt: msg.thinkingStartedAt ?? now,
          }
        : msg,
    );
  }
  return [
    ...messages,
    {
      id: msgId(),
      kind: 'assistant',
      text: '',
      toolCallId: 'active',
      assistantStatus: status,
      pending: true,
      thinkingStartedAt: now,
    },
  ];
}

function updateActiveAssistantStatus(messages: Message[], status: StatusLabel): Message[] {
  const index = findActiveAssistantIndex(messages);
  if (index < 0) return messages;
  return messages.map((msg, currentIndex) =>
    currentIndex === index ? { ...msg, assistantStatus: status } : msg,
  );
}

function appendAssistantText(messages: Message[], text: string, status: StatusLabel): Message[] {
  const now = Date.now();
  const index = findActiveAssistantIndex(messages);
  if (index >= 0) {
    return messages.map((msg, currentIndex) => {
      if (currentIndex !== index) return msg;
      const previousText = msg.text ?? '';
      const firstOutput = previousText.length === 0 && text.length > 0;
      return {
        ...msg,
        text: previousText + text,
        assistantStatus: 'responding',
        pending: false,
        thinkingStartedAt: msg.thinkingStartedAt ?? now,
        thinkingCompletedAt: firstOutput ? now : msg.thinkingCompletedAt,
      };
    });
  }
  return [
    ...messages,
    {
      id: msgId(),
      kind: 'assistant',
      text,
      toolCallId: 'active',
      assistantStatus: status === 'idle' ? 'responding' : status,
      pending: false,
      thinkingStartedAt: now,
      thinkingCompletedAt: now,
    },
  ];
}

export type Action =
  | { type: 'SET_SESSION'; sessionId: string; agentId?: string }
  | { type: 'SET_APP_PHASE'; phase: AppState['appPhase'] }
  | { type: 'SET_AGENT_SESSION'; sessionId: string; agentId: string }
  | { type: 'SET_METADATA'; metadata: RuntimeMetadata }
  | { type: 'SET_CAPABILITIES'; capabilities: UiCapabilities; fallback?: boolean }
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
      storyOverview?: StoryOverview | null;
      fallback?: boolean;
    }
  | { type: 'SET_OUTPUT_DIRECTORY'; path: string | null }
  | { type: 'REGISTER_GENERATED_FILE'; file: GeneratedFileEntry }
  | { type: 'SET_WORKSPACE_FILES'; files: WorkspaceFile[]; root?: string; fallback?: boolean }
  | { type: 'SET_FILE_SEARCH'; value: string }
  | { type: 'SET_FILE_TYPE_FILTER'; value: string }
  | { type: 'OPEN_GENERATED_FILE'; fileId: string }
  | { type: 'OPEN_WORKSPACE_FILE'; fileId: string }
  | { type: 'SET_EDITOR_CONTENT'; fileId: string; content: WorkspaceFileContent }
  | { type: 'UPDATE_EDITOR_BUFFER'; fileId: string; content: string }
  | { type: 'REVERT_EDITOR_BUFFER'; fileId: string }
  | { type: 'COMMIT_EDITOR_BUFFER'; fileId: string; version?: string }
  | { type: 'SET_EDITOR_SAVE_STATUS'; status: AppState['editorSaveStatus']; error?: string | null }
  | { type: 'SET_MODEL'; model: string }
  | { type: 'SET_LAST_SEQ'; seq: number }
  | { type: 'APPEND_MESSAGE'; message: Message }
  | { type: 'ENSURE_ASSISTANT_PENDING'; status?: StatusLabel }
  | { type: 'UPDATE_ASSISTANT'; text: string }
  | { type: 'SET_TOOL_RUNNING'; toolCallId: string; name: string; args: string }
  | { type: 'SET_TOOL_DONE'; toolCallId: string; output: string; isError: boolean }
  | { type: 'SET_APPROVAL'; approvalId: string; name: string; args: string }
  | { type: 'CLEAR_APPROVAL'; decision?: string }
  | { type: 'SET_STATUS'; status: StatusLabel }
  | { type: 'SET_USAGE'; inputTokens: number; outputTokens: number }
  | { type: 'SET_CURRENT_RUN'; runId: string | null }
  | { type: 'PIPELINE_WORKFLOW_ACTIVATED'; name: string }
  | { type: 'ADD_RUN_TIMELINE_ITEM'; item: RunTimelineItem }
  | { type: 'COMMIT_ACTIVE' }
  | { type: 'SET_STORY_VERSION' }
  | { type: 'APPLY_SSE'; frame: SseFrame }
  | { type: 'RESOLVE_ASK'; callId: string }
  | { type: 'RESOLVE_CREATION'; creationId: string }
  | { type: 'SET_PIPELINE_CONDITIONS'; conditions: ConditionState[] }
  | { type: 'SET_PIPELINE_VIEW'; view: Partial<PipelineViewData> }
  | { type: 'DISMISS_PHASE_PROMPT' }
  | { type: 'PIPELINE_ADVANCE_FAILED'; reason: string }
  | { type: 'PIPELINE_ERROR_CLEARED' }
  | { type: 'CLEAR_PHASE_ADVANCE_PROMPT' }
  | { type: 'CLEAR_CYCLE_COMPLETE' }
  | { type: 'SET_LLM_CONFIGURED'; configured: boolean }
  | { type: 'SHOW_SETUP_WIZARD'; show: boolean }
  | { type: 'SET_CHAPTER_REVIEW'; review: ChapterReviewItem | null }
  | { type: 'SET_SHOW_EXPORT_DIALOG'; show: boolean }
  | { type: 'SET_USER_PREFERENCES'; prefs: { default_genre: string; preferred_style: string } }
  | { type: 'SET_PAGE'; page: AppPage }
  | { type: 'OPEN_CHAPTER_EDITOR'; chapterId: string; chapterTitle: string }
  | { type: 'SET_PIPELINE_AUTO_ADVANCE'; value: boolean };

export function reducer(state: AppState, action: Action): AppState {
  switch (action.type) {
    case 'SET_APP_PHASE':
      return { ...state, appPhase: action.phase };

    case 'SET_AGENT_SESSION':
      return {
        ...state,
        sessionId: action.sessionId,
        agentId: action.agentId,
        appPhase: 'ready',
        messages: [],
        lastSeq: 0,
        currentRun: null,
        lastRunId: null,
        pendingAsk: null,
        pendingCreation: null,
        status: 'idle',
      };

    case 'SET_SESSION':
      return {
        ...state,
        sessionId: action.sessionId,
        agentId: action.agentId ?? state.agentId,
        messages: [],
        lastSeq: 0,
        currentRun: null,
        lastRunId: null,
        pendingAsk: null,
        pendingCreation: null,
        status: 'idle',
      };

    case 'SET_METADATA':
      return { ...state, metadata: action.metadata, selectedModel: action.metadata.model };

    case 'SET_CAPABILITIES':
      return {
        ...state,
        capabilities: action.capabilities,
        fallback: { ...state.fallback, capabilities: action.fallback ?? false },
      };

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
        storyOverview: null,
        worldbuildingStatus: action.worldId ? 'loading' : 'idle',
        worldbuildingError: null,
        appPhase: action.worldId ? 'no_agent' : state.appPhase,
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
        workspaceFiles: [
          workspaceFileFromGenerated(action.file),
          ...state.workspaceFiles.filter((file) => file.path !== action.file.path),
        ],
        outputDirectory:
          state.outputDirectory ?? directoryFromPath(action.file.path) ?? state.outputDirectory,
        inspectorTab: 'files',
      };

    case 'SET_WORKSPACE_FILES':
      return {
        ...state,
        workspaceFiles: action.files,
        outputDirectory: action.root ?? state.outputDirectory,
        fallback: { ...state.fallback, workspaceFiles: action.fallback ?? false },
      };

    case 'SET_FILE_SEARCH':
      return { ...state, fileSearch: action.value };

    case 'SET_FILE_TYPE_FILTER':
      return { ...state, fileTypeFilter: action.value };

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

    case 'OPEN_WORKSPACE_FILE': {
      const file = state.workspaceFiles.find((item) => item.id === action.fileId);
      if (!file) return state;
      return {
        ...state,
        activeEditorFileId: file.id,
        inspectorTab: 'files',
        editorBuffers: {
          ...state.editorBuffers,
          [file.id]: state.editorBuffers[file.id] ?? '',
        },
        editorSaveStatus: 'idle',
        editorError: null,
      };
    }

    case 'SET_EDITOR_CONTENT':
      return {
        ...state,
        editorBuffers: {
          ...state.editorBuffers,
          [action.fileId]: action.content.content,
        },
        editorOriginals: {
          ...state.editorOriginals,
          [action.fileId]: action.content.content,
        },
        editorVersions: {
          ...state.editorVersions,
          [action.fileId]: action.content.version,
        },
        editorSaveStatus: 'idle',
        editorError: null,
      };

    case 'UPDATE_EDITOR_BUFFER':
      return {
        ...state,
        editorBuffers: {
          ...state.editorBuffers,
          [action.fileId]: action.content,
        },
        workspaceFiles: state.workspaceFiles.map((file) =>
          file.id === action.fileId ? { ...file, dirty: true } : file,
        ),
        editorSaveStatus: 'dirty',
        editorError: null,
      };

    case 'REVERT_EDITOR_BUFFER':
      return {
        ...state,
        editorBuffers: {
          ...state.editorBuffers,
          [action.fileId]:
            state.editorOriginals[action.fileId] ?? state.editorBuffers[action.fileId] ?? '',
        },
        workspaceFiles: state.workspaceFiles.map((file) =>
          file.id === action.fileId ? { ...file, dirty: false } : file,
        ),
        editorSaveStatus: 'idle',
        editorError: null,
      };

    case 'COMMIT_EDITOR_BUFFER':
      return {
        ...state,
        editorOriginals: {
          ...state.editorOriginals,
          [action.fileId]: state.editorBuffers[action.fileId] ?? '',
        },
        editorVersions: {
          ...state.editorVersions,
          ...(action.version ? { [action.fileId]: action.version } : {}),
        },
      };

    case 'SET_EDITOR_SAVE_STATUS':
      return {
        ...state,
        editorSaveStatus: action.status,
        editorError: action.error ?? null,
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
        workspaceFiles: [
          ...files.map(workspaceFileFromGenerated),
          ...state.workspaceFiles.filter(
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
        storyOverview: action.storyOverview ?? state.storyOverview,
        worldbuildingStatus: 'ready',
        worldbuildingError: null,
        fallback: { ...state.fallback, storyOverview: action.fallback ?? false },
      };

    case 'SET_MODEL':
      return { ...state, selectedModel: action.model };

    case 'SET_LAST_SEQ':
      return { ...state, lastSeq: Math.max(state.lastSeq, action.seq) };

    case 'APPEND_MESSAGE':
      return { ...state, messages: [...state.messages, action.message] };

    case 'ENSURE_ASSISTANT_PENDING':
      return {
        ...state,
        messages: ensureActiveAssistantMessage(state.messages, action.status ?? state.status),
      };

    case 'UPDATE_ASSISTANT':
      return {
        ...state,
        messages: appendAssistantText(state.messages, action.text, state.status),
      };

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
      return {
        ...state,
        status: 'idle',
        messages: state.messages.map((m) =>
          m.kind === 'approval' && !m.approvalResolved
            ? { ...m, approvalResolved: true, approvalDecision: action.decision ?? 'resolved' }
            : m,
        ),
      };

    case 'SET_STORY_VERSION':
      return { ...state, storyVersion: state.storyVersion + 1 };

    case 'SET_STATUS':
      return {
        ...state,
        status: action.status,
        messages: updateActiveAssistantStatus(state.messages, action.status),
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
      return {
        ...state,
        currentRun: action.runId,
        lastRunId: action.runId ?? state.lastRunId,
      };

    case 'PIPELINE_WORKFLOW_ACTIVATED':
      return { ...state, pipelineActiveWorkflow: action.name };

    case 'ADD_RUN_TIMELINE_ITEM':
      return { ...state, runTimeline: [action.item, ...state.runTimeline].slice(0, 32) };

    case 'COMMIT_ACTIVE': {
      const now = Date.now();
      const msgs = state.messages.map((m) =>
        m.toolCallId === 'active'
          ? {
              ...m,
              toolCallId: undefined,
              pending: false,
              thinkingCompletedAt: m.thinkingCompletedAt ?? now,
            }
          : m,
      );
      return { ...state, messages: msgs };
    }

    case 'APPLY_SSE': {
      const { frame } = action;
      if (frame.seq > 0 && frame.seq <= state.lastSeq) return state;
      if (frame.seq > 0) state = { ...state, lastSeq: Math.max(state.lastSeq, frame.seq) };
      return applySseFrame(state, frame);
    }

    case 'RESOLVE_ASK':
      return state.pendingAsk?.callId === action.callId ? { ...state, pendingAsk: null } : state;

    case 'RESOLVE_CREATION':
      return state.pendingCreation?.id === action.creationId
        ? { ...state, pendingCreation: null }
        : state;

    case 'SET_PIPELINE_CONDITIONS':
      return { ...state, pipelineConditions: action.conditions };

    case 'SET_PIPELINE_VIEW': {
      const v = action.view;
      return {
        ...state,
        ...(v.phase !== undefined && { pipelinePhase: v.phase }),
        ...(v.conditions !== undefined && { pipelineConditions: v.conditions }),
        ...(v.active_workflow !== undefined && { pipelineActiveWorkflow: v.active_workflow }),
        ...(v.recent_history !== undefined && { pipelineHistory: v.recent_history }),
        ...(v.next_allowed !== undefined && { pipelineNextAllowed: v.next_allowed }),
        ...(v.allowed_retreat !== undefined && { pipelineAllowedRetreat: v.allowed_retreat }),
      };
    }

    case 'DISMISS_PHASE_PROMPT':
      return { ...state, showPhaseAdvancePrompt: null };

    case 'PIPELINE_ADVANCE_FAILED':
      return { ...state, pipelineAdvanceError: action.reason };

    case 'PIPELINE_ERROR_CLEARED':
      return { ...state, pipelineAdvanceError: undefined };

    case 'CLEAR_PHASE_ADVANCE_PROMPT':
      return { ...state, showPhaseAdvancePrompt: null };

    case 'CLEAR_CYCLE_COMPLETE':
      return { ...state, pipelineCycleComplete: null };

    case 'SET_LLM_CONFIGURED':
      return { ...state, llmConfigured: action.configured, showSetupWizard: false };

    case 'SHOW_SETUP_WIZARD':
      return { ...state, showSetupWizard: action.show };

    case 'SET_CHAPTER_REVIEW':
      return { ...state, chapterReview: action.review };

    case 'SET_SHOW_EXPORT_DIALOG':
      return { ...state, showExportDialog: action.show };

    case 'SET_USER_PREFERENCES':
      return { ...state, userPreferences: action.prefs };

    case 'SET_PAGE':
      return { ...state, currentPage: action.page };

    case 'OPEN_CHAPTER_EDITOR':
      return {
        ...state,
        currentPage: 'editor',
        activeEditorChapterId: action.chapterId,
        activeEditorChapterTitle: action.chapterTitle,
      };

    case 'SET_PIPELINE_AUTO_ADVANCE':
      return { ...state, pipelineAutoAdvance: action.value };

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
        reducer(
          reducer(
            reducer(
              reducer(state, { type: 'SET_CURRENT_RUN', runId: (p.run_id as string) ?? '' }),
              {
                type: 'APPEND_MESSAGE',
                message: { id: msgId(), kind: 'user', text: (p.message as string) ?? '' },
              },
            ),
            { type: 'SET_STATUS', status: 'thinking' },
          ),
          { type: 'ENSURE_ASSISTANT_PENDING', status: 'thinking' },
        ),
        {
          type: 'ADD_RUN_TIMELINE_ITEM',
          item: {
            id: `run_${p.run_id ?? frame.seq}`,
            type: 'state',
            label: 'Run started',
            detail: (p.message as string) ?? '',
            at: Date.now(),
            status: 'thinking',
          },
        },
      );

    case 'text_delta':
      return reducer(state, { type: 'UPDATE_ASSISTANT', text: (p.text as string) ?? '' });

    case 'state_changed': {
      const to = ((p.to as string) ?? '').toLowerCase();
      // Terminal states have their own events (run_completed/run_failed)
      if (to === 'complete' || to === 'error') return state;
      const validLabels = new Set(['thinking', 'responding', 'acting', 'observing']);
      const label = validLabels.has(to) ? (to as StatusLabel) : state.status;
      return reducer(
        reducer(reducer(state, { type: 'SET_STATUS', status: label }), {
          type: 'ENSURE_ASSISTANT_PENDING',
          status: label,
        }),
        {
          type: 'ADD_RUN_TIMELINE_ITEM',
          item: {
            id: `state_${frame.seq}_${to}`,
            type: 'state',
            label: `State: ${label.replace(/_/g, ' ')}`,
            at: Date.now(),
            status: label,
          },
        },
      );
    }

    case 'tool_started':
      return reducer(
        reducer(
          reducer(reducer(state, { type: 'SET_STATUS', status: 'acting' }), {
            type: 'ENSURE_ASSISTANT_PENDING',
            status: 'acting',
          }),
          {
            type: 'SET_TOOL_RUNNING',
            toolCallId: (p.id ?? p.tool_call_id ?? '') as string,
            name: (p.name ?? p.tool ?? '') as string,
            args: (p.arguments ?? '') as string,
          },
        ),
        {
          type: 'ADD_RUN_TIMELINE_ITEM',
          item: {
            id: `tool_${p.id ?? frame.seq}`,
            type: 'tool',
            label: `Tool: ${(p.name ?? p.tool ?? 'unknown') as string}`,
            detail: (p.arguments ?? '') as string,
            at: Date.now(),
            status: 'acting',
          },
        },
      );

    case 'tool_completed':
      return reducer(state, {
        type: 'SET_TOOL_DONE',
        toolCallId: (p.id ?? '') as string,
        output: (p.output ?? '') as string,
        isError: (p.is_error ?? false) as boolean,
      });

    case 'approval_requested':
      return reducer(
        reducer(state, {
          type: 'SET_APPROVAL',
          approvalId: (p.approval_id ?? '') as string,
          name: (p.name ?? p.tool ?? '') as string,
          args: (p.arguments ?? '') as string,
        }),
        {
          type: 'ADD_RUN_TIMELINE_ITEM',
          item: {
            id: `approval_${p.approval_id ?? frame.seq}`,
            type: 'approval',
            label: `Approval: ${(p.name ?? p.tool ?? 'tool') as string}`,
            detail: (p.arguments ?? '') as string,
            at: Date.now(),
            status: 'waiting_approval',
          },
        },
      );

    case 'approval_resolved':
      return reducer(state, { type: 'CLEAR_APPROVAL' });

    case 'ask_user_requested': {
      const choices = Array.isArray(p.options)
        ? p.options.filter((choice): choice is string => typeof choice === 'string')
        : undefined;
      return {
        ...state,
        pendingAsk: {
          runId: (p.run_id as string) || state.currentRun || '',
          callId: (p.call_id as string) ?? '',
          question: (p.question as string) ?? '',
          ...(choices?.length ? { choices } : {}),
          multiSelect: p.multi_select === true,
        },
      };
    }

    case 'creation_requested':
      return {
        ...state,
        pendingCreation: {
          id: (p.creation_id as string) ?? '',
          runId: (p.run_id as string) || state.currentRun || '',
          toolName: (p.tool as string) ?? '',
          ...(p.preview && typeof p.preview === 'object'
            ? { preview: p.preview as Record<string, unknown> }
            : {}),
        },
      };

    case 'creation_resolved': {
      const result = p.result as Record<string, unknown> | undefined;
      if (result?.ok === false) return state;
      const creationId = (p.creation_id as string) ?? '';
      return reducer(state, { type: 'RESOLVE_CREATION', creationId });
    }

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

    case 'run_completed': {
      state = clearPendingForTerminalRun(state, (p.run_id as string) || state.currentRun || '');
      return reducer(
        reducer(reducer(state, { type: 'COMMIT_ACTIVE' }), {
          type: 'SET_CURRENT_RUN',
          runId: null,
        }),
        { type: 'SET_STATUS', status: 'idle' },
      );
    }

    case 'run_failed':
    case 'run_cancelled':
    case 'run_interrupted':
      state = clearPendingForTerminalRun(state, (p.run_id as string) || state.currentRun || '');
      state = reducer(reducer(state, { type: 'COMMIT_ACTIVE' }), {
        type: 'SET_CURRENT_RUN',
        runId: null,
      });
      state = reducer(state, { type: 'SET_STATUS', status: 'idle' });
      if (type === 'run_failed') {
        return reducer(state, {
          type: 'APPEND_MESSAGE',
          message: {
            id: msgId(),
            kind: 'system',
            text: (p.error as string) ?? 'Run failed',
            error: true,
          },
        });
      }
      if (type === 'run_interrupted') {
        const turns = p.turns_completed ?? '?';
        const completed = p.tools_completed ?? '?';
        const remaining = p.tools_remaining ?? '?';
        const tool = p.interrupted_tool_name;
        const detail = tool
          ? `Run interrupted at turn ${turns} — ${completed} tools done, ${remaining} remaining (was about to run \`${tool}\`)`
          : `Run interrupted at turn ${turns}`;
        return reducer(state, {
          type: 'APPEND_MESSAGE',
          message: { id: msgId(), kind: 'system', text: detail, error: false },
        });
      }
      return state;

    case 'workspace_file_created': {
      const path = (p.path ?? '') as string;
      if (!path) return state;
      const file = workspaceFileFromGenerated({
        id: `file_${path}`,
        title: (p.name as string) || fileTitle(path),
        path,
        updatedAt: Date.now(),
      });
      return reducer(
        {
          ...state,
          workspaceFiles: [file, ...state.workspaceFiles.filter((item) => item.path !== path)],
          inspectorTab: 'files',
        },
        {
          type: 'ADD_RUN_TIMELINE_ITEM',
          item: {
            id: `file_${frame.seq}`,
            type: 'file',
            label: 'File created',
            detail: path,
            at: Date.now(),
            status: 'completed',
          },
        },
      );
    }

    case 'workspace_file_updated': {
      const path = (p.path ?? '') as string;
      if (!path) return state;
      return {
        ...state,
        workspaceFiles: state.workspaceFiles.map((file) =>
          file.path === path
            ? {
                ...file,
                dirty: false,
                updated_at: (p.updated_at as string) || new Date().toISOString(),
              }
            : file,
        ),
      };
    }

    case 'story_context_updated':
      return reducer(reducer(state, { type: 'SET_STORY_VERSION' }), {
        type: 'ADD_RUN_TIMELINE_ITEM',
        item: {
          id: `story_${frame.seq}`,
          type: 'state',
          label: `Story updated: ${(p.resource_type as string) || 'context'}`,
          detail: (p.resource_id as string) || '',
          at: Date.now(),
          status: 'completed',
        },
      });

    case 'run_step_changed': {
      const step = ((p.step as string) || 'thinking') as StatusLabel;
      return reducer(
        reducer(reducer(state, { type: 'SET_STATUS', status: step }), {
          type: 'ENSURE_ASSISTANT_PENDING',
          status: step,
        }),
        {
          type: 'ADD_RUN_TIMELINE_ITEM',
          item: {
            id: `run_step_${frame.seq}`,
            type: 'state',
            label: (p.label as string) || `Run step: ${step.replace(/_/g, ' ')}`,
            detail: (p.detail as string) || '',
            at: Date.now(),
            status: step,
          },
        },
      );
    }

    case 'scene_changed':
      return {
        ...state,
        storyVersion: state.storyVersion + 1,
      };

    case 'card_updated':
      return {
        ...state,
        storyVersion: state.storyVersion + 1,
      };

    case 'pipeline_phase_changed': {
      const conditions = Array.isArray(p.conditions)
        ? (p.conditions as ConditionState[])
        : state.pipelineConditions;
      return {
        ...state,
        pipelinePhase: typeof p.phase === 'string' ? p.phase : state.pipelinePhase,
        pipelineConditions: conditions,
        pipelineNextAllowed: Array.isArray(p.next_allowed)
          ? (p.next_allowed as string[])
          : state.pipelineNextAllowed,
        pipelineAllowedRetreat: Array.isArray(p.allowed_retreat)
          ? (p.allowed_retreat as string[])
          : state.pipelineAllowedRetreat,
        showPhaseAdvancePrompt: null,
        pipelineAdvanceError: undefined,
      };
    }

    case 'pipeline_advance_failed':
      return {
        ...state,
        pipelineAdvanceError: (p.reason || p.result || 'Unknown error') as string,
      };

    case 'pipeline_condition_progress': {
      const conditions = Array.isArray(p.conditions)
        ? (p.conditions as ConditionState[])
        : state.pipelineConditions;
      return { ...state, pipelineConditions: conditions };
    }

    case 'pipeline_condition_met':
      return {
        ...state,
        showPhaseAdvancePrompt: {
          phase: (p.phase as string) || '',
          nextPhase: (p.next_phase as string) || '',
          conditions: Array.isArray(p.conditions) ? (p.conditions as ConditionState[]) : [],
        },
      };

    case 'pipeline_cycle_complete':
      return {
        ...state,
        pipelineCycleComplete: {
          message: (p.message as string) || '创作管线全周期完成',
        },
      };

    case 'world_switched':
      return reducer(state, { type: 'SET_WORLD', worldId: (p.world_id as string) ?? null });

    default:
      return state;
  }
}

function clearPendingForTerminalRun(state: AppState, runId: string): AppState {
  if (!runId) return state;
  return {
    ...state,
    pendingAsk: state.pendingAsk?.runId === runId ? null : state.pendingAsk,
    pendingCreation: state.pendingCreation?.runId === runId ? null : state.pendingCreation,
  };
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

function workspaceFileFromGenerated(file: GeneratedFileEntry): WorkspaceFile {
  const name = file.path.split('/').filter(Boolean).pop() ?? file.title;
  const ext = name.includes('.') ? name.split('.').pop()?.toLowerCase() || '' : '';
  return {
    id: file.id,
    path: file.path,
    name,
    ext,
    mime: ext === 'md' || ext === 'markdown' ? 'text/markdown' : 'text/plain',
    size: 0,
    updated_at: new Date(file.updatedAt).toISOString(),
    dirty: false,
  };
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
