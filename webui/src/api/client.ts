const BASE = import.meta.env.VITE_API_BASE ?? '';

async function request(method: string, path: string, body?: unknown) {
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
    throw new Error((json as { error?: { message?: string } })?.error?.message ?? `HTTP ${res.status}`);
  }
  return json;
}

export const api = {
  metadata: () => request('GET', '/v1/runtime'),

  createSession: (title = '') => request('POST', '/v1/sessions', { title }),

  updateSession: (id: string, title: string) =>
    request('PATCH', `/v1/sessions/${id}`, { title }),

  generateTitle: (id: string) =>
    request('POST', `/v1/sessions/${id}/generate-title`),

  listSessions: () => request('GET', '/v1/sessions'),

  getSession: (id: string) => request('GET', `/v1/sessions/${id}`),

  events: (id: string, after = 0) => request('GET', `/v1/sessions/${id}/events?after=${after}`),

  memory: (id: string) => request('GET', `/v1/sessions/${id}/memory`),

  startRun: (id: string, message: string, model = '') =>
    request('POST', `/v1/sessions/${id}/runs`, { message, ...(model ? { model } : {}) }),

  startDelegation: (
    id: string,
    pattern: string,
    agents: string[],
    task: string,
    aggregation = 'all_results',
  ) => request('POST', `/v1/sessions/${id}/delegations`, { pattern, agents, task, aggregation }),

  resolveApproval: (id: string, allow: boolean) =>
    request('POST', `/v1/approvals/${id}`, { decision: allow ? 'allow' : 'deny' }),

  cancelRun: (id: string) => request('POST', `/v1/runs/${id}/cancel`),

  listWorlds: () => request('GET', '/api/worldbuilding/worlds'),

  updateWorld: (id: string, name?: string, description?: string) =>
    request('PATCH', `/api/worldbuilding/worlds/${id}`, { name, description }),

  listAgents: (worldId: string) => request('GET', `/api/worldbuilding/${worldId}/agents`),

  listForeshadowing: (worldId: string) =>
    request('GET', `/api/worldbuilding/${worldId}/foreshadowing`),

  listSecrets: (worldId: string) => request('GET', `/api/worldbuilding/${worldId}/secrets`),

  getWorldTime: (worldId: string) => request('GET', `/api/worldbuilding/${worldId}/time`),

  sseUrl: (id: string, after = 0) => `${BASE}/v1/sessions/${id}/events/stream?after=${after}`,

  getConfig: () => request('GET', '/api/config/llm'),

  saveConfig: (config: {
    provider?: string;
    api_key?: string;
    api_base_url?: string;
    default_model?: string;
    max_output_tokens?: number;
  }) => request('POST', '/api/config/llm', config),

  testConfig: () => request('POST', '/api/config/llm/test'),
};
