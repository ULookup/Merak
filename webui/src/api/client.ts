import type {
  AgentDetailResponse,
  AgentListResponse,
  AgentPromptResponse,
  ArchiveSessionResponse,
  ApprovalResponse,
  CancelRunResponse,
  CapabilitiesResponse,
  ChapterListResponse,
  CreateAgentResponse,
  CreateForeshadowingResponse,
  CreateSceneResponse,
  CreateSecretResponse,
  CreateSessionResponse,
  DiaryListResponse,
  EndSceneResponse,
  ForeshadowingListResponse,
  GenerateTitleResponse,
  LlmConfig,
  OkResponse,
  OpenWorkspacePathResponse,
  PatchAgentCardResponse,
  RelationListResponse,
  ResolveCreationResponse,
  RunAuditResponse,
  RunDetailResponse,
  RuntimeMetadata,
  SaveWorkspaceFileResponse,
  SecretListResponse,
  SceneListResponse,
  SessionListResponse,
  SessionSummary,
  StoryOverviewResponse,
  StartRunResponse,
  UpdateSessionResponse,
  UpdateWorldResponse,
  WorldDetailResponse,
  WorkspaceFileContentResponse,
  WorkspaceFileListResponse,
  WorldListResponse,
  WorldTimeResponse,
} from './types';

const BASE = import.meta.env.VITE_API_BASE ?? '';

export class ApiError extends Error {
  status: number;
  code?: string;
  constructor(message: string, status: number, code?: string) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
  }
}

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const opts: RequestInit = {
    method,
    headers: { 'Content-Type': 'application/json' },
  };
  if (body !== undefined) {
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(`${BASE}${path}`, opts);
  let json: unknown;
  try {
    json = await res.json();
  } catch {
    const text = await res.text().catch(() => '<unreadable>');
    throw new Error(`Non-JSON response (${res.status}): ${text.slice(0, 200)}`);
  }
  if (res.status >= 400) {
    const error = (json as { error?: { message?: string; code?: string } | string; message?: string }).error;
    const message =
      typeof error === 'string'
        ? error
        : (error?.message ?? (json as { message?: string }).message ?? `HTTP ${res.status}`);
    const code = (typeof error === 'object' && error !== null) ? error.code : undefined;
    throw new ApiError(message, res.status, code);
  }
  return json as T;
}

async function fallbackRequest<T>(path: string, fallback: T): Promise<T> {
  try {
    return await request<T>('GET', path);
  } catch {
    return fallback;
  }
}

async function fallbackMutation<T>(method: string, path: string, body: unknown, fallback: T) {
  try {
    return await request<T>(method, path, body);
  } catch {
    return fallback;
  }
}

function nowIso() {
  return new Date().toISOString();
}

const fallbackCapabilities: CapabilitiesResponse = {
  ok: true,
  fallback: true,
  capabilities: {
    files: false,
    story_overview: false,
    session_archive: false,
    world_create: false,
    editor_save: false,
  },
};

function fallbackOverview(worldId: string): StoryOverviewResponse {
  return {
    ok: true,
    fallback: true,
    overview: {
      fallback: true,
      current_arc: {
        id: `arc_${worldId || 'preview'}`,
        title: 'Frontier arc preview',
        status: 'drafting',
        purpose: 'Backend overview is pending; this preview keeps the workbench usable.',
      },
      current_chapter: {
        id: `chapter_${worldId || 'preview'}`,
        title: 'Chapter workspace',
        number: 1,
        status: 'drafting',
        scene_count: 3,
        updated_at: nowIso(),
      },
      current_scene: {
        id: `scene_${worldId || 'preview'}`,
        title: 'Next scene beat',
        chapter_id: `chapter_${worldId || 'preview'}`,
        world_time: 'Day 1 Dawn',
        status: 'writing',
        participant_ids: [],
        updated_at: nowIso(),
      },
      agents: [],
      foreshadowing: [
        {
          id: 'mock_foreshadowing_1',
          content: 'A small clue waits to be paid off after the next conflict.',
          status: 'open',
          hint_level: 'visible',
          tags: ['preview'],
        },
      ],
      secrets: [
        {
          id: 'mock_secret_1',
          title: 'Knowledge boundary preview',
          truth: 'Only the author knows the full answer until the backend endpoint lands.',
          public_version: 'Characters only see a partial explanation.',
          stakes: 'Avoid accidental omniscience in generated scenes.',
          status: 'active',
          aware_character_ids: [],
          suspicious_character_ids: [],
        },
      ],
      world_time: 'Day 1 Dawn',
    },
  };
}

function fallbackFiles(sessionId?: string, worldId?: string): WorkspaceFileListResponse {
  const stamp = nowIso();
  const root = worldId ? `~/.merak/worlds/${worldId}/outputs` : '~/.merak/outputs';
  return {
    ok: true,
    root,
    fallback: true,
    files: [
      {
        id: `mock_outline_${sessionId || 'session'}`,
        path: `${root}/chapter-01-outline.md`,
        name: 'chapter-01-outline.md',
        ext: 'md',
        mime: 'text/markdown',
        size: 2480,
        updated_at: stamp,
        generated_by_run_id: undefined,
        dirty: false,
        fallback: true,
      },
      {
        id: `mock_scene_${sessionId || 'session'}`,
        path: `${root}/scene-beat-notes.md`,
        name: 'scene-beat-notes.md',
        ext: 'md',
        mime: 'text/markdown',
        size: 1260,
        updated_at: stamp,
        generated_by_run_id: undefined,
        dirty: false,
        fallback: true,
      },
    ],
  };
}

function fallbackFileContent(path: string): WorkspaceFileContentResponse {
  return {
    ok: true,
    fallback: true,
    file: {
      path,
      encoding: 'utf-8',
      updated_at: nowIso(),
      version: 'mock-1',
      fallback: true,
      content: `# ${path.split('/').pop() || 'Draft'}\n\nThis is a local WebUI preview buffer. The backend file content endpoint is not implemented yet.\n\n- Review the scene goal.\n- Tighten character knowledge boundaries.\n- Save will remain local until /api/workspace/files/content is available.\n`,
    },
  };
}

export const api = {
  metadata: () => request<RuntimeMetadata>('GET', '/v1/runtime'),

  capabilities: () =>
    fallbackRequest<CapabilitiesResponse>('/api/webui/capabilities', fallbackCapabilities),

  createSession: (title = '') => request<CreateSessionResponse>('POST', '/v1/sessions', { title }),

  archiveSession: (session: SessionSummary, archived: boolean) =>
    fallbackMutation<ArchiveSessionResponse>(
      'POST',
      `/v1/sessions/${session.id}/archive`,
      { archived },
      {
        ok: true,
        fallback: true,
        session: { ...session, archived_at: archived ? nowIso() : null },
      },
    ),

  updateSession: (id: string, title: string) =>
    request<UpdateSessionResponse>('PATCH', `/v1/sessions/${id}`, { title }),

  generateTitle: (id: string) =>
    request<GenerateTitleResponse>('POST', `/v1/sessions/${id}/generate-title`),

  listSessions: () => request<SessionListResponse>('GET', '/v1/sessions'),

  getSession: (id: string) => request<SessionSummary>('GET', `/v1/sessions/${id}`),

  getRun: (runId: string, sessionId = '') =>
    fallbackRequest<RunDetailResponse>(`/v1/runs/${runId}`, {
      ok: true,
      fallback: true,
      run: {
        id: runId,
        session_id: sessionId,
        status: 'streaming',
        model: '',
        started_at: nowIso(),
        input_tokens: 0,
        output_tokens: 0,
        tool_calls: [],
      },
    }),

  events: (id: string, after = 0) =>
    request<{ events: unknown[] }>('GET', `/v1/sessions/${id}/events?after=${after}`),

  memory: (id: string) =>
    request<{ session_id: string; items: unknown[] }>('GET', `/v1/sessions/${id}/memory`),

  startRun: (id: string, message: string, model = '') =>
    request<StartRunResponse>('POST', `/v1/sessions/${id}/runs`, {
      message,
      ...(model ? { model } : {}),
    }),

  startDelegation: (
    id: string,
    pattern: string,
    agents: string[],
    task: string,
    aggregation = 'all_results',
  ) =>
    request<{ delegation_id: string; parent_run_id: string; session_id: string }>(
      'POST',
      `/v1/sessions/${id}/delegations`,
      { pattern, agents, task, aggregation },
    ),

  resolveApproval: (id: string, allow: boolean) =>
    request<ApprovalResponse>('POST', `/v1/approvals/${id}`, {
      decision: allow ? 'allow' : 'deny',
    }),

  cancelRun: (id: string) => request<CancelRunResponse>('POST', `/v1/runs/${id}/cancel`),

  listWorlds: () => request<WorldListResponse>('GET', '/api/worldbuilding/worlds'),

  createWorld: (name: string, description = '') =>
    fallbackMutation<UpdateWorldResponse>(
      'POST',
      '/api/worldbuilding/worlds',
      { name, description },
      {
        ok: true,
        world_id: `preview_world_${Date.now()}`,
        name,
        description,
      },
    ),

  updateWorld: (id: string, name?: string, description?: string) =>
    request<UpdateWorldResponse>('PATCH', `/api/worldbuilding/worlds/${id}`, {
      name,
      description,
    }),

  listAgents: (worldId: string) =>
    request<AgentListResponse>('GET', `/api/worldbuilding/${worldId}/agents`),

  listForeshadowing: (worldId: string) =>
    request<ForeshadowingListResponse>('GET', `/api/worldbuilding/${worldId}/foreshadowing`),

  listSecrets: (worldId: string) =>
    request<SecretListResponse>('GET', `/api/worldbuilding/${worldId}/secrets`),

  getWorldTime: (worldId: string) =>
    request<WorldTimeResponse>('GET', `/api/worldbuilding/${worldId}/time`),

  getStoryOverview: (worldId: string, sessionId = '') =>
    fallbackRequest<StoryOverviewResponse>(
      `/api/worldbuilding/${worldId}/overview${sessionId ? `?session_id=${encodeURIComponent(sessionId)}` : ''}`,
      fallbackOverview(worldId),
    ),

  listChapters: (worldId: string, status = '') => {
    const chapter = fallbackOverview(worldId).overview.current_chapter;
    return fallbackRequest<ChapterListResponse>(
      `/api/worldbuilding/${worldId}/chapters${status ? `?status=${encodeURIComponent(status)}` : ''}`,
      { ok: true, fallback: true, chapters: chapter ? [chapter] : [] },
    );
  },

  fetchChapterContent: (worldId: string, chapterId: string) =>
    request<{ ok: boolean; content?: string }>('GET', `/api/worldbuilding/${worldId}/chapters/${chapterId}`),

  listScenes: (worldId: string, chapterId = '', status = '') => {
    const params = new URLSearchParams();
    if (chapterId) params.set('chapter_id', chapterId);
    if (status) params.set('status', status);
    const query = params.toString();
    const scene = fallbackOverview(worldId).overview.current_scene;
    return fallbackRequest<SceneListResponse>(
      `/api/worldbuilding/${worldId}/scenes${query ? `?${query}` : ''}`,
      { ok: true, fallback: true, scenes: scene ? [scene] : [] },
    );
  },

  listWorkspaceFiles: (query: {
    session_id?: string;
    world_id?: string;
    root?: string;
    q?: string;
    type?: string;
  }) => {
    const params = new URLSearchParams();
    for (const [key, value] of Object.entries(query)) {
      if (value) params.set(key, value);
    }
    const fallback = fallbackFiles(query.session_id, query.world_id);
    const term = query.q?.toLowerCase();
    const type = query.type?.toLowerCase();
    if (term || type) {
      fallback.files = fallback.files.filter((file) => {
        const matchesTerm = !term || file.name.toLowerCase().includes(term) || file.path.toLowerCase().includes(term);
        const matchesType = !type || type === 'all' || file.ext.toLowerCase() === type;
        return matchesTerm && matchesType;
      });
    }
    return fallbackRequest<WorkspaceFileListResponse>(
      `/api/workspace/files${params.toString() ? `?${params}` : ''}`,
      fallback,
    );
  },

  readWorkspaceFile: (path: string) =>
    fallbackRequest<WorkspaceFileContentResponse>(
      `/api/workspace/files/content?path=${encodeURIComponent(path)}`,
      fallbackFileContent(path),
    ),

  saveWorkspaceFile: (path: string, content: string, version?: string) =>
    fallbackMutation<SaveWorkspaceFileResponse>(
      'PUT',
      '/api/workspace/files/content',
      { path, content, version },
      { ok: true, fallback: true, file: { path, updated_at: nowIso(), version: version ?? 'mock-1' } },
    ),

  sseUrl: (id: string) => `${BASE}/v1/sessions/${id}/events/stream`,

  getConfig: () => request<LlmConfig>('GET', '/api/config/llm'),

  saveConfig: (config: {
    provider?: string;
    api_key?: string;
    api_base_url?: string;
    default_model?: string;
    max_output_tokens?: number;
  }) => request<OkResponse>('POST', '/api/config/llm', config),

  testConfig: () => request<OkResponse>('POST', '/api/config/llm/test'),

  openWorkspacePath: (path: string, reveal = false) =>
    request<OpenWorkspacePathResponse>('POST', '/api/workspace/open', { path, reveal }),

  // Agent detail + card
  fetchAgentDetail: (worldId: string, agentId: string) =>
    request<AgentDetailResponse>('GET', `/api/worldbuilding/${worldId}/agents/${agentId}`),

  patchAgentCard: (worldId: string, agentId: string, fields: Record<string, unknown>, version: number) =>
    request<PatchAgentCardResponse>('PATCH', `/api/worldbuilding/${worldId}/agents/${agentId}`, { fields, version }),

  // Diaries
  fetchDiaries: (worldId: string, agentId: string) =>
    request<DiaryListResponse>('GET', `/api/worldbuilding/${worldId}/agents/${agentId}/diaries`),

  addDiary: (worldId: string, agentId: string, sceneId: string | undefined, content: string, worldTime?: string) =>
    request<OkResponse>('POST', `/api/worldbuilding/${worldId}/agents/${agentId}/diaries`, {
      scene_id: sceneId,
      content,
      world_time: worldTime,
    }),

  // Relations
  fetchRelations: (worldId: string, agentId: string) =>
    request<RelationListResponse>('GET', `/api/worldbuilding/${worldId}/agents/${agentId}/relations`),

  // Patch other cards
  patchForeshadow: (worldId: string, id: string, fields: Record<string, unknown>) =>
    request<OkResponse>('PATCH', `/api/worldbuilding/${worldId}/foreshadowing/${id}`, { fields }),

  patchScene: (worldId: string, id: string, fields: Record<string, unknown>) =>
    request<OkResponse>('PATCH', `/api/worldbuilding/${worldId}/scenes/${id}`, { fields }),

  patchChapter: (worldId: string, id: string, fields: Record<string, unknown>) =>
    request<OkResponse>('PATCH', `/api/worldbuilding/${worldId}/chapters/${id}`, { fields }),

  patchSecret: (worldId: string, id: string, fields: Record<string, unknown>) =>
    request<OkResponse>('PATCH', `/api/worldbuilding/${worldId}/secrets/${id}`, { fields }),

  // Run audit
  fetchRunAudit: (runId: string) =>
    request<RunAuditResponse>('GET', `/v1/runs/${runId}/audit`),

  // Create mutations
  createAgent: (worldId: string, data: {
    name: string;
    gender?: string;
    age?: number;
    race?: string;
    identity?: string;
    emotional_tendency?: string;
    speaking_style?: string;
    core_desire?: string;
    deep_fear?: string;
    daily_goal?: string;
    background?: string;
    knowledge_scope?: string;
    appearance?: string;
    core_traits?: string[];
    taboo_topics?: string[];
    version?: number;
    session_id?: string;
  }) =>
    request<CreateAgentResponse>('POST', `/api/worldbuilding/${worldId}/agents`, data),

  createScene: (worldId: string, data: {
    title?: string;
    name?: string;
    chapter_id: string;
    world_time?: string;
    narrative?: string;
    section_id?: string;
    location_id?: string;
    participant_ids?: string[];
    session_id?: string;
  }) =>
    request<CreateSceneResponse>('POST', `/api/worldbuilding/${worldId}/scenes`, data),

  endScene: (worldId: string, sceneId: string, data: {
    final_markdown?: string;
    session_id?: string;
  }) =>
    request<EndSceneResponse>('POST', `/api/worldbuilding/${worldId}/scenes/${sceneId}/end`, data),

  createForeshadowing: (worldId: string, data: {
    content: string;
    hint?: string;
    pay_off_idea?: string;
    hint_level?: string;
    tags?: string[];
    session_id?: string;
  }) =>
    request<CreateForeshadowingResponse>('POST', `/api/worldbuilding/${worldId}/foreshadowing`, data),

  createSecret: (worldId: string, data: {
    title?: string;
    holder_id?: string;
    truth?: string;
    public_version?: string;
    stakes?: string;
    aware_character_ids?: string[];
    suspicious_character_ids?: string[];
    related_foreshadowing_ids?: string[];
    session_id?: string;
  }) =>
    request<CreateSecretResponse>('POST', `/api/worldbuilding/${worldId}/secrets`, data),

  // Prompt
  getAgentPrompt: (agentId: string) =>
    request<AgentPromptResponse>('GET', `/api/worldbuilding/agents/${agentId}/prompt`),

  // World detail
  getWorldDetail: (worldId: string) =>
    request<WorldDetailResponse>('GET', `/api/worldbuilding/worlds/${worldId}`),

  // Creation resolution — wired for future SSE-driven creation approval flow
  // The backend emits creation requests via SSE; the UI will call this to allow/deny with optional modifications.
  resolveCreation: (id: string, decision: string, modifications?: Record<string, unknown>) =>
    request<ResolveCreationResponse>('POST', `/v1/creations/${id}/resolve`, { decision, modifications }),
};
