import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

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
      'http://127.0.0.1:3888/v1/runtime',
      expect.objectContaining({ method: 'GET' })
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
});
