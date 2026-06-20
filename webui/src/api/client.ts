import { ApiError, apiUrl, fallbackRequest, request, requestBlob, requestForm } from './http';
import { runtimeApi } from './runtime';
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
  MemorySummaryListResponse,
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
  VoiceFingerprintResponse,
  WorkflowSummary,
  WorkspaceFileContentResponse,
  WorkspaceFileListResponse,
  WorldDetailResponse,
  WorldListResponse,
  WorldTimeResponse,
} from './types';

export { ApiError, apiUrl, formatApiError, getApiBase, setApiBase } from './http';

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
  ...runtimeApi,

  capabilities: () =>
    fallbackRequest<CapabilitiesResponse>('/api/webui/capabilities', fallbackCapabilities),

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
    request<SaveWorkspaceFileResponse>('PUT', '/api/workspace/files/content', {
      path,
      content,
      version,
    }),

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
  getPreferences: () => request<PreferencesResponse>('GET', '/api/config/preferences'),

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

  fetchMemorySummaries: (worldId: string, agentId: string) =>
    request<MemorySummaryListResponse>(
      'GET',
      `/api/worldbuilding/${encodeURIComponent(worldId)}/agents/${encodeURIComponent(agentId)}/memory-summaries`,
    ),

  fetchAgentVoice: async (worldId: string, agentId: string) => {
    try {
      return await request<VoiceFingerprintResponse>(
        'GET',
        `/api/worldbuilding/${encodeURIComponent(worldId)}/agents/${encodeURIComponent(agentId)}/voice`,
      );
    } catch (error) {
      if (error instanceof ApiError && error.status === 404) return { ok: true, voice: null };
      throw error;
    }
  },

  deleteAgent: (worldId: string, agentId: string) =>
    request<OkResponse>(
      'DELETE',
      `/api/worldbuilding/${encodeURIComponent(worldId)}/agents/${encodeURIComponent(agentId)}`,
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
