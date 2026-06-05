const BASE = 'http://127.0.0.1:3888';

async function request(method: string, path: string, body?: unknown) {
  const opts: RequestInit = {
    method,
    headers: { 'Content-Type': 'application/json' },
  };
  if (body !== undefined) {
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(`${BASE}${path}`, opts);
  const json = await res.json();
  if (res.status >= 400) {
    throw new Error(json?.error?.message ?? `HTTP ${res.status}`);
  }
  return json;
}

export const api = {
  metadata: () => request('GET', '/v1/runtime'),

  createSession: (title = '') =>
    request('POST', '/v1/sessions', { title }),

  listSessions: () => request('GET', '/v1/sessions'),

  getSession: (id: string) => request('GET', `/v1/sessions/${id}`),

  events: (id: string, after = 0) =>
    request('GET', `/v1/sessions/${id}/events?after=${after}`),

  memory: (id: string) => request('GET', `/v1/sessions/${id}/memory`),

  startRun: (id: string, message: string, model = '') =>
    request('POST', `/v1/sessions/${id}/runs`, { message, ...(model ? { model } : {}) }),

  startDelegation: (
    id: string,
    pattern: string,
    agents: string[],
    task: string,
    aggregation = 'all_results'
  ) =>
    request('POST', `/v1/sessions/${id}/delegations`, { pattern, agents, task, aggregation }),

  resolveApproval: (id: string, allow: boolean) =>
    request('POST', `/v1/approvals/${id}`, { decision: allow ? 'allow' : 'deny' }),

  cancelRun: (id: string) => request('POST', `/v1/runs/${id}/cancel`),

  listWorlds: () => request('GET', '/api/worldbuilding/worlds'),

  sseUrl: (id: string, after = 0) =>
    `${BASE}/v1/sessions/${id}/events/stream?after=${after}`,
};
