import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

describe('api client', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', vi.fn());
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('metadata() calls GET /v1/runtime', async () => {
    vi.mocked(fetch).mockResolvedValue({
      status: 200,
      json: async () => ({ model: 'gpt-4o' }),
    } as Response);

    const { api } = await import('../api/client');
    const result = await api.metadata();
    expect(result.model).toBe('gpt-4o');
    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      '/v1/runtime',
      expect.objectContaining({ method: 'GET' }),
    );
  });

  it('createSession() calls POST /v1/sessions', async () => {
    vi.mocked(fetch).mockResolvedValue({
      status: 201,
      json: async () => ({ session_id: 's1' }),
    } as Response);

    const { api } = await import('../api/client');
    const result = await api.createSession('test');
    expect(result.session_id).toBe('s1');
    const [, opts] = vi.mocked(fetch).mock.calls[0];
    expect(opts?.method).toBe('POST');
    expect(JSON.parse((opts as RequestInit).body as string)).toEqual({ title: 'test' });
  });

  it('throws on HTTP error', async () => {
    vi.mocked(fetch).mockResolvedValue({
      status: 409,
      json: async () => ({ error: { message: 'session busy' } }),
    } as Response);

    const { api } = await import('../api/client');
    await expect(api.startRun('s1', 'hi')).rejects.toThrow('session busy');
  });

  it('does not invent a world when world creation is unavailable', async () => {
    vi.mocked(fetch).mockResolvedValue({
      status: 501,
      json: async () => ({ error: { message: 'world creation unavailable' } }),
    } as Response);

    const { api } = await import('../api/client');
    await expect(api.createWorld('Northreach')).rejects.toThrow('world creation unavailable');
  });

  it('does not return mock story overview data when the backend is unavailable', async () => {
    vi.mocked(fetch).mockResolvedValue({
      status: 503,
      json: async () => ({ error: { message: 'story overview unavailable' } }),
    } as Response);

    const { api } = await import('../api/client');
    await expect(api.getStoryOverview('world_1')).rejects.toThrow('story overview unavailable');
  });

  it('does not return mock workspace files when the backend is unavailable', async () => {
    vi.mocked(fetch).mockResolvedValue({
      status: 404,
      json: async () => ({ error: { message: 'files endpoint missing' } }),
    } as Response);

    const { api } = await import('../api/client');
    await expect(api.listWorkspaceFiles({ world_id: 'world_1' })).rejects.toThrow(
      'files endpoint missing',
    );
  });

  it('worldbuilding readers call the selected world endpoints', async () => {
    vi.mocked(fetch).mockResolvedValue({
      status: 200,
      json: async () => ({ ok: true, agents: [] }),
    } as Response);

    const { api } = await import('../api/client');
    await api.listAgents('world_1');
    await api.listForeshadowing('world_1');
    await api.listSecrets('world_1');
    await api.getWorldTime('world_1');

    expect(vi.mocked(fetch)).toHaveBeenNthCalledWith(
      1,
      '/api/worldbuilding/world_1/agents',
      expect.objectContaining({ method: 'GET' }),
    );
    expect(vi.mocked(fetch)).toHaveBeenNthCalledWith(
      2,
      '/api/worldbuilding/world_1/foreshadowing',
      expect.objectContaining({ method: 'GET' }),
    );
    expect(vi.mocked(fetch)).toHaveBeenNthCalledWith(
      3,
      '/api/worldbuilding/world_1/secrets',
      expect.objectContaining({ method: 'GET' }),
    );
    expect(vi.mocked(fetch)).toHaveBeenNthCalledWith(
      4,
      '/api/worldbuilding/world_1/time',
      expect.objectContaining({ method: 'GET' }),
    );
  });
});
