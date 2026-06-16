import type {
  AdvanceWorldTimeResponse,
  AgentDetailResponse,
  AgentImageListResponse,
  AgentImageType,
  AgentImageUploadResponse,
  AgentListResponse,
  AgentPromptResponse,
  ApprovalResponse,
  ArchiveSessionResponse,
  CancelRunResponse,
  CapabilitiesResponse,
  ChapterListResponse,
  ChapterReviewResponse,
  ChunkedImageInitResponse,
  ChunkedImageProgressResponse,
  CreateAgentResponse,
  CreateForeshadowingResponse,
  CreateSceneResponse,
  CreateSecretResponse,
  CreateSessionResponse,
  DeleteWorldResponse,
  DiaryListResponse,
  EndSceneResponse,
  ExportRequest,
  ExportResult,
  ForeshadowingListResponse,
  GenerateTitleResponse,
  LlmConfigFull,
  OkResponse,
  OpenWorkspacePathResponse,
  PatchAgentCardResponse,
  PipelineHistoryResponse,
  PipelineViewData,
  PreferencesResponse,
  RelationListResponse,
  ResolveCreationResponse,
  RunAuditResponse,
  RunDetailResponse,
  RuntimeMetadata,
  SaveWorkspaceFileResponse,
  SceneListResponse,
  SecretListResponse,
  SessionListResponse,
  SessionSummary,
  StartRunResponse,
  StoryOverviewResponse,
  UpdateSessionResponse,
  UpdateWorldResponse,
  WorkflowSummary,
  WorkspaceFileContentResponse,
  WorkspaceFileListResponse,
  WorldDetailResponse,
  WorldListResponse,
  WorldTimeResponse,
} from './types';

let apiBase = import.meta.env.VITE_API_BASE ?? '';

export function setApiBase(base: string) {
  apiBase = base.replace(/\/$/, '');
}

export function getApiBase() {
  return apiBase;
}

export function apiUrl(path: string) {
  if (/^https?:\/\//i.test(path)) return path;
  return `${apiBase}${path.startsWith('/') ? path : `/${path}`}`;
}

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

export function formatApiError(error: unknown, fallback = '操作失败，请稍后重试。') {
  if (error instanceof ApiError) {
    if (error.code === 'version_conflict') return '内容已在后端更新，请刷新后再保存。';
    if (error.code === 'file_conflict') return '文件已被其他操作修改，请刷新后再保存。';
    if (error.code === 'pipeline_not_available')
      return 'Pipeline 暂不可用，请确认后端已启用 worldbuilding pipeline。';
    if (error.code === 'image_service_not_available')
      return '图片服务未启用，请确认后端 Image Service 已初始化。';
    if (error.code === 'invalid_image_type') return '图片类型必须是头像或人设图。';
    if (error.code === 'image_not_found') return '图片不存在或已被删除。';
    if (error.code === 'test_failed') return `连接测试失败：${error.message}`;
    if (error.code === 'test_unavailable') return '连接测试暂不可用，请检查后端配置。';
    return error.message || fallback;
  }
  return error instanceof Error ? error.message : fallback;
}

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const opts: RequestInit = {
    method,
    headers: { 'Content-Type': 'application/json' },
  };
  if (body !== undefined) {
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(apiUrl(path), opts);
  return parseJsonResponse<T>(res);
}

async function parseJsonResponse<T>(res: Response): Promise<T> {
  let json: unknown;
  try {
    json = await res.json();
  } catch {
    const text = await res.text().catch(() => '<unreadable>');
    throw new Error(`Non-JSON response (${res.status}): ${text.slice(0, 200)}`);
  }
  if (res.status >= 400) {
    const error = (
      json as { error?: { message?: string; code?: string } | string; message?: string }
    ).error;
    const message =
      typeof error === 'string'
        ? error
        : (error?.message ?? (json as { message?: string }).message ?? `HTTP ${res.status}`);
    const code = typeof error === 'object' && error !== null ? error.code : undefined;
    throw new ApiError(message, res.status, code);
  }
  return json as T;
}

async function requestForm<T>(path: string, body: FormData): Promise<T> {
  const res = await fetch(apiUrl(path), { method: 'POST', body });
  return parseJsonResponse<T>(res);
}

async function requestBlob<T>(method: string, path: string, body?: Blob): Promise<T> {
  const res = await fetch(apiUrl(path), { method, body });
  return parseJsonResponse<T>(res);
}

async function fallbackRequest<T>(path: string, fallback: T): Promise<T> {
  try {
    return await request<T>('GET', path);
  } catch {
    return fallback;
  }
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

export const api = {
  metadata: () => request<RuntimeMetadata>('GET', '/v1/runtime'),

  capabilities: () =>
    fallbackRequest<CapabilitiesResponse>('/api/webui/capabilities', fallbackCapabilities),

  createSession: (title = '', worldId?: string, agentId?: string) => {
    const body: Record<string, string> = { title };
    if (worldId) body.world_id = worldId;
    if (agentId) body.agent_id = agentId;
    return request<CreateSessionResponse>('POST', '/v1/sessions', body);
  },

  archiveSession: (session: SessionSummary, archived: boolean) =>
    request<ArchiveSessionResponse>('POST', `/v1/sessions/${session.id}/archive`, { archived }),

  updateSession: (id: string, title: string) =>
    request<UpdateSessionResponse>('PATCH', `/v1/sessions/${id}`, { title }),

  generateTitle: (id: string) =>
    request<GenerateTitleResponse>('POST', `/v1/sessions/${id}/generate-title`),

  listSessions: (worldId?: string) => {
    const query = worldId ? `?world_id=${encodeURIComponent(worldId)}` : '';
    return request<SessionListResponse>('GET', `/v1/sessions${query}`);
  },

  getOrCreateAgentSession: (worldId: string, agentId: string) =>
    request<{ session: SessionSummary; created: boolean }>(
      'GET',
      `/v1/worlds/${encodeURIComponent(worldId)}/agents/${encodeURIComponent(agentId)}/session`,
    ),

  getSession: (id: string) => request<SessionSummary>('GET', `/v1/sessions/${id}`),

  getRun: (runId: string) => request<RunDetailResponse>('GET', `/v1/runs/${runId}`),

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
    request<UpdateWorldResponse>('POST', '/api/worldbuilding/worlds', { name, description }),

  updateWorld: (id: string, name?: string, description?: string) =>
    request<UpdateWorldResponse>('PATCH', `/api/worldbuilding/worlds/${id}`, {
      name,
      description,
    }),

  deleteWorld: (id: string) =>
    request<DeleteWorldResponse>('DELETE', `/api/worldbuilding/worlds/${id}`),

  listAgents: (worldId: string) =>
    request<AgentListResponse>('GET', `/api/worldbuilding/${worldId}/agents`),

  listForeshadowing: (worldId: string) =>
    request<ForeshadowingListResponse>('GET', `/api/worldbuilding/${worldId}/foreshadowing`),

  listSecrets: (worldId: string) =>
    request<SecretListResponse>('GET', `/api/worldbuilding/${worldId}/secrets`),

  getWorldTime: (worldId: string) =>
    request<WorldTimeResponse>('GET', `/api/worldbuilding/${worldId}/time`),

  advanceWorldTime: (worldId: string, worldTime: string) =>
    request<AdvanceWorldTimeResponse>('POST', `/api/worldbuilding/${worldId}/time/advance`, {
      world_time: worldTime,
    }),

  getStoryOverview: (worldId: string, sessionId = '') =>
    request<StoryOverviewResponse>(
      'GET',
      `/api/worldbuilding/${worldId}/overview${sessionId ? `?session_id=${encodeURIComponent(sessionId)}` : ''}`,
    ),

  listChapters: (worldId: string, status = '') => {
    return request<ChapterListResponse>(
      'GET',
      `/api/worldbuilding/${worldId}/chapters${status ? `?status=${encodeURIComponent(status)}` : ''}`,
    );
  },

  fetchChapterContent: (worldId: string, chapterId: string) =>
    request<{ ok: boolean; content?: string }>(
      'GET',
      `/api/worldbuilding/${worldId}/chapters/${chapterId}`,
    ),

  listScenes: (worldId: string, chapterId = '', status = '') => {
    const params = new URLSearchParams();
    if (chapterId) params.set('chapter_id', chapterId);
    if (status) params.set('status', status);
    const query = params.toString();
    return request<SceneListResponse>(
      'GET',
      `/api/worldbuilding/${worldId}/scenes${query ? `?${query}` : ''}`,
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
    return request<WorkspaceFileListResponse>(
      'GET',
      `/api/workspace/files${params.toString() ? `?${params}` : ''}`,
    );
  },

  readWorkspaceFile: (path: string) =>
    request<WorkspaceFileContentResponse>(
      'GET',
      `/api/workspace/files/content?path=${encodeURIComponent(path)}`,
    ),

  saveWorkspaceFile: (path: string, content: string, version?: string) =>
    request<SaveWorkspaceFileResponse>(
      'PUT',
      '/api/workspace/files/content',
      { path, content, version },
    ),

  sseUrl: (id: string) => `${apiBase}/v1/sessions/${id}/events/stream`,

  getConfig: () => request<LlmConfigFull>('GET', '/api/config/llm'),

  saveConfig: (config: {
    provider?: string;
    api_key?: string;
    api_base_url?: string;
    default_model?: string;
    max_output_tokens?: number;
    temperature?: number;
    context_memory_length?: 'short' | 'medium' | 'long';
    writer_model?: string;
  }) => request<OkResponse>('POST', '/api/config/llm', config),

  testConfig: () => request<OkResponse>('POST', '/api/config/llm/test'),

  // Preferences
  getPreferences: () =>
    request<PreferencesResponse>('GET', '/api/config/preferences'),

  savePreferences: (prefs: {
    default_genre?: string;
    preferred_style?: string;
    allow_usage_logs?: boolean;
  }) => request<OkResponse>('PUT', '/api/config/preferences', prefs),

  // Chapter review
  getChapterReview: (worldId: string, chapterId: string) =>
    request<ChapterReviewResponse>(
      'GET',
      `/api/worldbuilding/${encodeURIComponent(worldId)}/chapters/${encodeURIComponent(chapterId)}/review`,
    ),

  // Export
  exportChapters: (worldId: string, data: ExportRequest) =>
    request<ExportResult>('POST', `/api/worldbuilding/${encodeURIComponent(worldId)}/export`, data),

  openWorkspacePath: (path: string, reveal = false) =>
    request<OpenWorkspacePathResponse>('POST', '/api/workspace/open', { path, reveal }),

  // Agent detail + card
  fetchAgentDetail: (worldId: string, agentId: string) =>
    request<AgentDetailResponse>('GET', `/api/worldbuilding/${worldId}/agents/${agentId}`),

  patchAgentCard: (
    worldId: string,
    agentId: string,
    fields: Record<string, unknown>,
    version: number,
  ) =>
    request<PatchAgentCardResponse>('PATCH', `/api/worldbuilding/${worldId}/agents/${agentId}`, {
      fields,
      version,
    }),

  imageUrl: (path: string) => apiUrl(path),

  listAgentImages: (worldId: string, agentId: string) =>
    request<AgentImageListResponse>(
      'GET',
      `/api/worldbuilding/${worldId}/agents/${agentId}/images`,
    ),

  uploadAgentImage: (worldId: string, agentId: string, imageType: AgentImageType, file: File) => {
    const form = new FormData();
    form.set('image_type', imageType);
    form.set('file', file);
    return requestForm<AgentImageUploadResponse>(
      `/api/worldbuilding/${worldId}/agents/${agentId}/images`,
      form,
    );
  },

  initAgentImageUpload: (
    worldId: string,
    agentId: string,
    data: {
      image_type: AgentImageType;
      file_name: string;
      mime_type: string;
      total_size: number;
      chunk_size: number;
    },
  ) =>
    request<ChunkedImageInitResponse>(
      'POST',
      `/api/worldbuilding/${worldId}/agents/${agentId}/images/chunked`,
      data,
    ),

  uploadAgentImageChunk: (
    worldId: string,
    agentId: string,
    uploadId: string,
    chunkIdx: number,
    chunk: Blob,
  ) =>
    requestBlob<OkResponse>(
      'PUT',
      `/api/worldbuilding/${worldId}/agents/${agentId}/images/chunked/${uploadId}?chunk_idx=${chunkIdx}`,
      chunk,
    ),

  getAgentImageUploadProgress: (worldId: string, agentId: string, uploadId: string) =>
    request<ChunkedImageProgressResponse>(
      'GET',
      `/api/worldbuilding/${worldId}/agents/${agentId}/images/chunked/${uploadId}`,
    ),

  completeAgentImageUpload: (worldId: string, agentId: string, uploadId: string) =>
    request<AgentImageUploadResponse>(
      'POST',
      `/api/worldbuilding/${worldId}/agents/${agentId}/images/chunked/${uploadId}/complete`,
    ),

  cancelAgentImageUpload: (worldId: string, agentId: string, uploadId: string) =>
    request<OkResponse>(
      'DELETE',
      `/api/worldbuilding/${worldId}/agents/${agentId}/images/chunked/${uploadId}`,
    ),

  updateAgentImage: (
    worldId: string,
    agentId: string,
    imageId: string,
    fields: { is_primary?: boolean; sort_order?: number },
  ) =>
    request<OkResponse>(
      'PATCH',
      `/api/worldbuilding/${worldId}/agents/${agentId}/images/${imageId}`,
      fields,
    ),

  deleteAgentImage: (worldId: string, agentId: string, imageId: string) =>
    request<OkResponse>(
      'DELETE',
      `/api/worldbuilding/${worldId}/agents/${agentId}/images/${imageId}`,
    ),

  // Diaries
  fetchDiaries: (worldId: string, agentId: string) =>
    request<DiaryListResponse>('GET', `/api/worldbuilding/${worldId}/agents/${agentId}/diaries`),

  addDiary: (
    worldId: string,
    agentId: string,
    sceneId: string | undefined,
    content: string,
    worldTime?: string,
  ) =>
    request<OkResponse>('POST', `/api/worldbuilding/${worldId}/agents/${agentId}/diaries`, {
      scene_id: sceneId,
      content,
      world_time: worldTime,
    }),

  // Relations
  fetchRelations: (worldId: string, agentId: string) =>
    request<RelationListResponse>(
      'GET',
      `/api/worldbuilding/${worldId}/agents/${agentId}/relations`,
    ),

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
  fetchRunAudit: (runId: string) => request<RunAuditResponse>('GET', `/v1/runs/${runId}/audit`),

  // Create mutations
  createAgent: (
    worldId: string,
    data: {
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
    },
  ) => request<CreateAgentResponse>('POST', `/api/worldbuilding/${worldId}/agents`, data),

  createScene: (
    worldId: string,
    data: {
      title?: string;
      name?: string;
      chapter_id: string;
      world_time?: string;
      narrative?: string;
      section_id?: string;
      location_id?: string;
      participant_ids?: string[];
      session_id?: string;
    },
  ) => request<CreateSceneResponse>('POST', `/api/worldbuilding/${worldId}/scenes`, data),

  endScene: (
    worldId: string,
    sceneId: string,
    data: {
      final_markdown?: string;
      session_id?: string;
    },
  ) =>
    request<EndSceneResponse>('POST', `/api/worldbuilding/${worldId}/scenes/${sceneId}/end`, data),

  createForeshadowing: (
    worldId: string,
    data: {
      content: string;
      hint?: string;
      pay_off_idea?: string;
      hint_level?: string;
      tags?: string[];
      session_id?: string;
    },
  ) =>
    request<CreateForeshadowingResponse>(
      'POST',
      `/api/worldbuilding/${worldId}/foreshadowing`,
      data,
    ),

  createSecret: (
    worldId: string,
    data: {
      title?: string;
      holder_id?: string;
      truth?: string;
      public_version?: string;
      stakes?: string;
      aware_character_ids?: string[];
      suspicious_character_ids?: string[];
      related_foreshadowing_ids?: string[];
      session_id?: string;
    },
  ) => request<CreateSecretResponse>('POST', `/api/worldbuilding/${worldId}/secrets`, data),

  // Prompt
  getAgentPrompt: (agentId: string) =>
    request<AgentPromptResponse>('GET', `/api/worldbuilding/agents/${agentId}/prompt`),

  // World detail
  getWorldDetail: (worldId: string) =>
    request<WorldDetailResponse>('GET', `/api/worldbuilding/worlds/${worldId}`),

  // Creation resolution — wired for future SSE-driven creation approval flow
  // The backend emits creation requests via SSE; the UI will call this to allow/deny with optional modifications.
  resolveCreation: (id: string, decision: string, modifications?: Record<string, unknown>) =>
    request<ResolveCreationResponse>('POST', `/v1/creations/${id}/resolve`, {
      decision,
      modifications,
    }),
};

export async function getPipelineState(worldId: string): Promise<PipelineViewData> {
  const res = await fetch(apiUrl(`/api/worldbuilding/${worldId}/pipeline/state`));
  if (!res.ok) throw new Error(`Failed to get pipeline state: ${res.status}`);
  return res.json();
}

export async function advancePipeline(
  worldId: string,
  body: { target_phase?: string; force?: boolean },
): Promise<void> {
  const res = await fetch(apiUrl(`/api/worldbuilding/${worldId}/pipeline/advance`), {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  if (!res.ok) {
    const err = await res.json();
    throw new Error(err.error || 'advance failed');
  }
}

export async function listPipelineWorkflows(): Promise<WorkflowSummary[]> {
  const res = await fetch(apiUrl('/api/worldbuilding/pipeline/workflows'));
  if (!res.ok) throw new Error(`Failed to list workflows: ${res.status}`);
  const data = await res.json();
  return data.workflows;
}

export async function activatePipelineWorkflow(
  worldId: string,
  workflowName: string,
): Promise<void> {
  const res = await fetch(apiUrl(`/api/worldbuilding/${worldId}/pipeline/activate`), {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ workflow_name: workflowName }),
  });
  if (!res.ok) throw new Error(`Failed to activate workflow: ${res.status}`);
}

export async function listPipelineHistory(
  worldId: string,
  limit = 10,
): Promise<PipelineHistoryResponse> {
  const params = new URLSearchParams({
    world_id: worldId,
    limit: String(limit),
  });
  const res = await fetch(apiUrl(`/api/worldbuilding/pipeline/history?${params}`));
  if (!res.ok) throw new Error(`Failed to list pipeline history: ${res.status}`);
  return res.json();
}
