import { getApiBase, request } from './http';
import type {
  ApprovalResponse,
  ArchiveSessionResponse,
  AskResponse,
  CancelRunResponse,
  CreateSessionResponse,
  GenerateTitleResponse,
  ResolveCreationResponse,
  RunDetailResponse,
  RuntimeMetadata,
  SessionListResponse,
  SessionSummary,
  StartRunResponse,
  UpdateSessionResponse,
} from './types';

export const runtimeApi = {
  metadata: () => request<RuntimeMetadata>('GET', '/v1/runtime'),

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
  respondToAsk: (runId: string, callId: string, response: string) =>
    request<AskResponse>('POST', `/v1/runs/${runId}/ask-response`, {
      call_id: callId,
      response,
    }),
  resolveCreation: (id: string, decision: string, modifications?: Record<string, unknown>) =>
    request<ResolveCreationResponse>('POST', `/v1/creations/${id}/resolve`, {
      decision,
      modifications,
    }),
  sseUrl: (id: string) => `${getApiBase()}/v1/sessions/${id}/events/stream`,
};
