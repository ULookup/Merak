import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { filesApi, worldbuildingApi } from '../api';
import { request } from '../api/http';

describe('api client', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', vi.fn());
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it.each([
    [
      { error: { code: 'world_not_found', message: 'World not found', retryable: false } },
      'world_not_found',
    ],
    [
      { ok: false, error: { code: 'file_conflict', message: 'Conflict', retryable: true } },
      'file_conflict',
    ],
    [{ error: 'session not found' }, undefined],
  ])('normalizes documented error shapes', async (payload, code) => {
    vi.mocked(fetch).mockResolvedValueOnce(new Response(JSON.stringify(payload), { status: 409 }));
    await expect(request('GET', '/failure')).rejects.toMatchObject({ status: 409, code });
  });

  it('rejects non-success 3xx responses with ApiError', async () => {
    vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 304 }));

    await expect(request('GET', '/not-modified')).rejects.toMatchObject({
      name: 'ApiError',
      status: 304,
    });
  });

  it('metadata() calls GET /v1/runtime', async () => {
    vi.mocked(fetch).mockResolvedValue({
      ok: true,
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
      ok: true,
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
      ok: true,
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

  it('saveConfig() sends max_output_tokens to the backend', async () => {
    vi.mocked(fetch).mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ ok: true }),
    } as Response);

    const { api } = await import('../api/client');
    await api.saveConfig({
      provider: 'openai',
      api_key: 'sk-test',
      api_base_url: 'https://api.openai.com/v1',
      default_model: 'gpt-4o',
      max_output_tokens: 8192,
    });

    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      '/api/config/llm',
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({
          provider: 'openai',
          api_key: 'sk-test',
          api_base_url: 'https://api.openai.com/v1',
          default_model: 'gpt-4o',
          max_output_tokens: 8192,
        }),
      }),
    );
  });

  it('deleteWorld() calls DELETE /api/worldbuilding/worlds/:id', async () => {
    vi.mocked(fetch).mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ ok: true, deleted: 'world_1' }),
    } as Response);

    const { api } = await import('../api/client');
    await api.deleteWorld('world_1');

    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      '/api/worldbuilding/worlds/world_1',
      expect.objectContaining({ method: 'DELETE' }),
    );
  });

  it('advanceWorldTime() calls POST /api/worldbuilding/:worldId/time/advance', async () => {
    vi.mocked(fetch).mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ ok: true, world_time: 'Day 2 Dawn' }),
    } as Response);

    const { api } = await import('../api/client');
    await api.advanceWorldTime('world_1', 'Day 2 Dawn');

    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      '/api/worldbuilding/world_1/time/advance',
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({ world_time: 'Day 2 Dawn' }),
      }),
    );
  });

  it('reorders chapters with the documented body', async () => {
    vi.mocked(fetch).mockResolvedValueOnce(
      new Response(JSON.stringify({ ok: true }), { status: 200 }),
    );

    await worldbuildingApi.reorderChapters('w 1', ['c1', 'c2']);

    expect(vi.mocked(fetch)).toHaveBeenCalledWith(
      expect.stringContaining('/api/worldbuilding/w%201/chapters/reorder'),
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({ chapter_ids: ['c1', 'c2'] }),
      }),
    );
  });

  it('encodes a linked file path when deleting it', async () => {
    vi.mocked(fetch).mockResolvedValueOnce(
      new Response(JSON.stringify({ ok: true }), { status: 200 }),
    );

    await filesApi.unlinkWorldFile('w1', '章节/第一章.md');

    expect(vi.mocked(fetch).mock.calls[0][0]).toContain(encodeURIComponent('章节/第一章.md'));
  });
});
