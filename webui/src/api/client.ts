import type {
  AgentListResponse,
  ApprovalResponse,
  CancelRunResponse,
  CreateSessionResponse,
  ForeshadowingListResponse,
  GenerateTitleResponse,
  LlmConfig,
  OkResponse,
  OpenWorkspacePathResponse,
  RuntimeMetadata,
  SecretListResponse,
  SessionListResponse,
  SessionSummary,
  StartRunResponse,
  UpdateSessionResponse,
  UpdateWorldResponse,
  WorldListResponse,
  WorldTimeResponse,
} from './types';

const BASE = import.meta.env.VITE_API_BASE ?? '';

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
    const error = (json as { error?: { message?: string } | string; message?: string }).error;
    const message =
      typeof error === 'string'
        ? error
        : (error?.message ?? (json as { message?: string }).message ?? `HTTP ${res.status}`);
    throw new Error(message);
  }
  return json as T;
}

export const api = {
  metadata: () => request<RuntimeMetadata>('GET', '/v1/runtime'),

  createSession: (title = '') => request<CreateSessionResponse>('POST', '/v1/sessions', { title }),

  updateSession: (id: string, title: string) =>
    request<UpdateSessionResponse>('PATCH', `/v1/sessions/${id}`, { title }),

  generateTitle: (id: string) =>
    request<GenerateTitleResponse>('POST', `/v1/sessions/${id}/generate-title`),

  listSessions: () => request<SessionListResponse>('GET', '/v1/sessions'),

  getSession: (id: string) => request<SessionSummary>('GET', `/v1/sessions/${id}`),

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
};
