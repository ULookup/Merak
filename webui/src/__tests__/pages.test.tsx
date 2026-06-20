import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { useState } from 'react';
import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { api } from '../api/client';
import { worldbuildingApi } from '../api/worldbuilding';
import { AppStateProvider } from '../AppState';
import DetailPane from '../components/layout/DetailPane';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import { useResource } from '../hooks/useResource';
import CharactersPage from '../pages/CharactersPage';
import OverviewPage from '../pages/OverviewPage';
import { selectWorldMetrics } from '../pages/selectors';
import WorldPage from '../pages/WorldPage';

vi.mock('../api/client', () => ({
  api: {
    listAgents: vi.fn(),
    listChapters: vi.fn(),
    listScenes: vi.fn(),
    listWorkspaceFiles: vi.fn(),
    getStoryOverview: vi.fn(),
    getWorldDetail: vi.fn(),
    fetchAgentDetail: vi.fn(),
    fetchDiaries: vi.fn(),
    fetchRelations: vi.fn(),
    createAgent: vi.fn(),
    fetchMemorySummaries: vi.fn(),
    fetchAgentVoice: vi.fn(),
    deleteAgent: vi.fn(),
  },
}));

vi.mock('../api/worldbuilding', () => ({
  worldbuildingApi: {
    getDashboard: vi.fn(),
    listLocations: vi.fn(),
    listKnowledge: vi.fn(),
    listFactions: vi.fn(),
    getTimeline: vi.fn(),
    listGraphEntities: vi.fn(),
  },
}));

type Deferred<T> = {
  promise: Promise<T>;
  resolve(value: T): void;
  reject(error: unknown): void;
};

function deferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void;
  let reject!: (error: unknown) => void;
  const promise = new Promise<T>((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  return { promise, resolve, reject };
}

function ResourceHarness({
  resourceKey,
  loader,
  onRender,
}: {
  resourceKey: string;
  loader: (signal: AbortSignal) => Promise<string>;
  onRender?: (key: string, data: string | null) => void;
}) {
  const resource = useResource(resourceKey, loader);
  onRender?.(resourceKey, resource.data);
  return (
    <div>
      <output aria-label="status">{resource.status}</output>
      <output aria-label="data">{resource.data ?? 'none'}</output>
      <output aria-label="error">{resource.error?.message ?? 'none'}</output>
      <button type="button" onClick={resource.retry}>
        Retry resource
      </button>
    </div>
  );
}

describe('useResource', () => {
  it('does not expose data from the previous key during the new key render', async () => {
    const renders: Array<{ key: string; data: string | null }> = [];
    const loader = vi
      .fn()
      .mockResolvedValueOnce('first data')
      .mockReturnValue(new Promise(() => {}));
    const onRender = (key: string, data: string | null) => renders.push({ key, data });
    const view = render(
      <ResourceHarness resourceKey="first" loader={loader} onRender={onRender} />,
    );

    await waitFor(() => expect(screen.getByLabelText('data')).toHaveTextContent('first data'));
    const renderCountBeforeKeyChange = renders.length;
    view.rerender(<ResourceHarness resourceKey="second" loader={loader} onRender={onRender} />);

    expect(renders[renderCountBeforeKeyChange]).toEqual({ key: 'second', data: null });
  });

  it('aborts the previous request when the resource key changes', async () => {
    const first = deferred<string>();
    const second = deferred<string>();
    const signals: AbortSignal[] = [];
    const loader = vi.fn((signal: AbortSignal) => {
      signals.push(signal);
      return signals.length === 1 ? first.promise : second.promise;
    });
    const view = render(<ResourceHarness resourceKey="first" loader={loader} />);

    await waitFor(() => expect(loader).toHaveBeenCalledTimes(1));
    view.rerender(<ResourceHarness resourceKey="second" loader={loader} />);

    await waitFor(() => expect(loader).toHaveBeenCalledTimes(2));
    expect(signals[0].aborted).toBe(true);
    expect(screen.getByLabelText('data')).toHaveTextContent('none');

    await act(async () => second.resolve('second data'));
    expect(screen.getByLabelText('data')).toHaveTextContent('second data');
  });

  it('retains successful data when a retry fails', async () => {
    const retryRequest = deferred<string>();
    const loader = vi
      .fn<(signal: AbortSignal) => Promise<string>>()
      .mockResolvedValueOnce('retained data')
      .mockImplementationOnce(() => retryRequest.promise);

    render(<ResourceHarness resourceKey="resource" loader={loader} />);
    await waitFor(() => expect(screen.getByLabelText('status')).toHaveTextContent('ready'));

    fireEvent.click(screen.getByRole('button', { name: 'Retry resource' }));
    expect(screen.getByLabelText('status')).toHaveTextContent('loading');
    expect(screen.getByLabelText('data')).toHaveTextContent('retained data');

    await act(async () => retryRequest.reject(new Error('Refresh failed')));
    expect(screen.getByLabelText('status')).toHaveTextContent('error');
    expect(screen.getByLabelText('data')).toHaveTextContent('retained data');
    expect(screen.getByLabelText('error')).toHaveTextContent('Refresh failed');
  });

  it('ignores an aborted request rejection', async () => {
    const request = deferred<string>();
    render(<ResourceHarness resourceKey="resource" loader={() => request.promise} />);

    await act(async () => request.reject(new DOMException('Aborted', 'AbortError')));

    expect(screen.getByLabelText('status')).toHaveTextContent('loading');
    expect(screen.getByLabelText('error')).toHaveTextContent('none');
  });
});

describe('PageState', () => {
  it('shows a non-blocking warning while retaining stale content after refresh failure', () => {
    const retry = vi.fn();
    render(
      <PageState error={new Error('Refresh failed')} hasData onRetry={retry}>
        <p>Retained content</p>
      </PageState>,
    );

    expect(screen.getByRole('alert')).toHaveTextContent('Refresh failed');
    expect(screen.getByText('Retained content')).toBeDefined();
    fireEvent.click(screen.getByRole('button', { name: 'Retry' }));
    expect(retry).toHaveBeenCalledOnce();
  });

  it('renders loading, full error, and empty states without page-specific copy', () => {
    const { rerender } = render(<PageState loading loadingLabel="Loading worlds" />);
    expect(screen.getByRole('status')).toHaveAccessibleName('Loading worlds');

    rerender(<PageState error={new Error('Worlds unavailable')} onRetry={() => {}} />);
    expect(screen.getByRole('alert')).toHaveTextContent('Worlds unavailable');

    rerender(<PageState isEmpty emptyTitle="No worlds" emptyDescription="Create one to begin." />);
    expect(screen.getByRole('heading', { name: 'No worlds' })).toBeDefined();
    expect(screen.getByText('Create one to begin.')).toBeDefined();
  });
});

describe('ResourceList', () => {
  const items = [
    { id: 'a', name: 'A' },
    { id: 'b', name: 'B' },
  ];

  function SelectableList({ initialId = 'a' }: { initialId?: string }) {
    const [selectedId, setSelectedId] = useState(initialId);
    return (
      <ResourceList
        items={items}
        selectedId={selectedId}
        getId={(item) => item.id}
        renderItem={(item) => item.name}
        onSelect={setSelectedId}
      />
    );
  }

  it('scrolls keyboard-selected options into view', () => {
    const scrollIntoView = vi.fn();
    Object.defineProperty(HTMLElement.prototype, 'scrollIntoView', {
      configurable: true,
      value: scrollIntoView,
    });

    render(<SelectableList />);
    scrollIntoView.mockClear();
    fireEvent.keyDown(screen.getByRole('listbox'), { key: 'End' });

    expect(screen.getByRole('option', { name: 'B' })).toHaveAttribute('aria-selected', 'true');
    expect(scrollIntoView).toHaveBeenCalledWith({ block: 'nearest' });

    scrollIntoView.mockClear();
    fireEvent.keyDown(screen.getByRole('listbox'), { key: 'Home' });
    expect(screen.getByRole('option', { name: 'A' })).toHaveAttribute('aria-selected', 'true');
    expect(scrollIntoView).toHaveBeenCalledWith({ block: 'nearest' });

    scrollIntoView.mockClear();
    fireEvent.keyDown(screen.getByRole('listbox'), { key: 'ArrowDown' });
    expect(screen.getByRole('option', { name: 'B' })).toHaveAttribute('aria-selected', 'true');
    expect(scrollIntoView).toHaveBeenCalledWith({ block: 'nearest' });

    delete HTMLElement.prototype.scrollIntoView;
  });

  it('focuses the listbox when an option is clicked', () => {
    render(<SelectableList />);

    fireEvent.click(screen.getByRole('option', { name: 'B' }));

    const listbox = screen.getByRole('listbox');
    expect(listbox).toHaveFocus();
    fireEvent.keyDown(listbox, { key: 'ArrowUp' });
    expect(screen.getByRole('option', { name: 'A' })).toHaveAttribute('aria-selected', 'true');
  });

  it('selects the next item with the keyboard and exposes the selected option', () => {
    const onSelect = vi.fn();
    render(
      <ResourceList
        ariaLabel="Characters"
        items={items}
        selectedId="a"
        getId={(item) => item.id}
        renderItem={(item) => item.name}
        onSelect={onSelect}
      />,
    );

    const listbox = screen.getByRole('listbox', { name: 'Characters' });
    expect(screen.getByRole('option', { name: 'A' })).toHaveAttribute('aria-selected', 'true');
    fireEvent.keyDown(listbox, { key: 'ArrowDown' });
    expect(onSelect).toHaveBeenCalledWith('b');
  });

  it('supports boundary navigation and mouse selection', () => {
    const onSelect = vi.fn();
    render(
      <ResourceList
        items={items}
        selectedId="b"
        getId={(item) => item.id}
        renderItem={(item) => item.name}
        onSelect={onSelect}
      />,
    );

    const listbox = screen.getByRole('listbox');
    fireEvent.keyDown(listbox, { key: 'ArrowDown' });
    expect(onSelect).toHaveBeenLastCalledWith('b');
    fireEvent.keyDown(listbox, { key: 'Home' });
    expect(onSelect).toHaveBeenLastCalledWith('a');
    fireEvent.click(screen.getByRole('option', { name: 'B' }));
    expect(onSelect).toHaveBeenLastCalledWith('b');
  });
});

describe('DetailPane', () => {
  it('provides a labeled detail region with actions and an optional inspector', () => {
    render(
      <DetailPane
        title="Lian"
        description="Archivist"
        actions={<button type="button">Edit</button>}
        inspector={<p>Related chapters</p>}
      >
        <p>Character details</p>
      </DetailPane>,
    );

    expect(screen.getByRole('region', { name: 'Lian' })).toBeDefined();
    expect(screen.getByRole('button', { name: 'Edit' })).toBeDefined();
    expect(screen.getByRole('complementary')).toHaveTextContent('Related chapters');
  });
});

describe('Overview selectors', () => {
  it('derives counts from real response-shaped resource fixtures', () => {
    expect(
      selectWorldMetrics({
        agents: [{ id: 'a' }],
        chapters: [{ id: 'c', status: 'completed' }],
        scenes: [],
        files: [],
        overview: null,
        dashboard: null,
      }),
    ).toMatchObject({ characterCount: 1, chapterCount: 1, completedChapterCount: 1 });
  });

  it('uses dashboard aggregates when available and derives missing values', () => {
    expect(
      selectWorldMetrics({
        agents: [{ id: 'a' }],
        chapters: [{ id: 'c', status: 'drafting' }],
        scenes: [{ id: 's', status: 'completed' }],
        files: [{ id: 'f' }],
        overview: null,
        dashboard: {
          agents: { total: 4 },
          chapters: { total: 3, completed: 2 },
          progress: { chapter_completion_pct: 67 },
        },
      }),
    ).toMatchObject({
      characterCount: 4,
      chapterCount: 3,
      completedChapterCount: 2,
      sceneCount: 1,
      fileCount: 1,
      chapterCompletionPercent: 67,
    });
  });

  it('counts revised dashboard chapters as completed progress', () => {
    expect(
      selectWorldMetrics({
        agents: [],
        chapters: null,
        scenes: [],
        files: [],
        overview: null,
        dashboard: {
          chapters: { total: 1, completed: 0, revised: 1 },
          progress: { chapter_completion_pct: 100 },
        },
      }),
    ).toMatchObject({
      chapterCount: 1,
      completedChapterCount: 1,
      chapterCompletionPercent: 100,
    });
  });

  it('does not substitute dashboard file-link associations for workspace files', () => {
    expect(
      selectWorldMetrics({
        agents: [],
        chapters: [],
        scenes: [],
        files: null,
        overview: null,
        dashboard: { file_links: { total: 9 } },
      }),
    ).toMatchObject({ fileCount: null, fileSource: null });
  });
});

describe('Overview page', () => {
  const navigate = vi.fn();

  function mockResources({
    dashboardFails = false,
    empty = false,
    failedLists = [],
  }: {
    dashboardFails?: boolean;
    empty?: boolean;
    failedLists?: Array<'agents' | 'chapters' | 'scenes' | 'files'>;
  } = {}) {
    vi.mocked(api.listAgents).mockResolvedValue({
      ok: true,
      agents: empty ? [] : [{ id: 'a', name: 'Lin', display_name: 'Lin', kind: 'character' }],
    });
    vi.mocked(api.listChapters).mockResolvedValue({
      ok: true,
      chapters: empty
        ? []
        : [
            {
              id: 'c',
              title: 'The First Light',
              number: 1,
              status: 'drafting',
              scene_count: 1,
              updated_at: '2026-06-18T12:00:00Z',
            },
          ],
    });
    vi.mocked(api.listScenes).mockResolvedValue({ ok: true, scenes: [] });
    vi.mocked(api.listWorkspaceFiles).mockResolvedValue({ ok: true, root: '', files: [] });
    vi.mocked(api.getStoryOverview).mockResolvedValue({
      ok: true,
      overview: { agents: [], foreshadowing: [], secrets: [], world_time: null },
    });
    const dashboard = {
      agents: { total: 1 },
      chapters: { total: 1, completed: 0 },
      progress: { chapter_completion_pct: 0 },
    };
    vi.mocked(worldbuildingApi.getDashboard).mockImplementation(() =>
      dashboardFails
        ? Promise.reject(new Error('Dashboard unavailable'))
        : Promise.resolve({ ok: true, dashboard: empty ? {} : dashboard }),
    );
    if (failedLists.includes('agents')) {
      vi.mocked(api.listAgents).mockRejectedValue(new Error('Agents unavailable'));
    }
    if (failedLists.includes('chapters')) {
      vi.mocked(api.listChapters).mockRejectedValue(new Error('Chapters unavailable'));
    }
    if (failedLists.includes('scenes')) {
      vi.mocked(api.listScenes).mockRejectedValue(new Error('Scenes unavailable'));
    }
    if (failedLists.includes('files')) {
      vi.mocked(api.listWorkspaceFiles).mockRejectedValue(new Error('Files unavailable'));
    }
  }

  it('Overview renders API-backed metrics, sessions, reminders, and quick links', async () => {
    mockResources();
    render(
      <OverviewPage
        worldId="world-1"
        sessions={[
          {
            id: 'session-1',
            title: 'Night planning',
            world_id: 'world-1',
            agent_id: null,
            last_seq: 3,
            created_at: '2026-06-17T10:00:00Z',
            updated_at: '2026-06-19T10:00:00Z',
            archived_at: null,
          },
        ]}
        onNavigate={navigate}
      />,
    );

    expect(await screen.findByText('Night planning')).toBeDefined();
    expect(screen.getByText('The First Light')).toBeDefined();
    expect(screen.getByRole('button', { name: 'Sessions' })).toBeDefined();
    expect(screen.getByRole('button', { name: 'Settings' })).toBeDefined();
    expect(screen.queryByRole('button', { name: 'View characters' })).toBeNull();
    expect(screen.queryByRole('button', { name: 'Chapters' })).toBeNull();
    expect(screen.queryByRole('button', { name: 'Files' })).toBeNull();
    expect(screen.getByText('Characters').closest('article')).toHaveAttribute(
      'title',
      'Counted from the worldbuilding dashboard.',
    );
    expect(screen.getByText('Characters').closest('article')).toHaveTextContent('1');
    expect(screen.getByRole('progressbar', { name: 'Chapter completion' })).toHaveAttribute(
      'title',
      'Calculated from the worldbuilding dashboard.',
    );
    expect(screen.getByRole('progressbar', { name: 'Scene completion' })).toHaveAttribute(
      'title',
      'Calculated from scene statuses in the scene list.',
    );
  });

  it('Overview falls back to derived resource counts when dashboard fails', async () => {
    mockResources({ dashboardFails: true });
    render(<OverviewPage worldId="world-1" sessions={[]} onNavigate={navigate} />);

    expect(await screen.findByText(/Dashboard summary is unavailable\./)).toBeDefined();
    expect(screen.getByTitle('Counted from the world character list.')).toHaveTextContent('1');
    expect(screen.getByText('The First Light')).toBeDefined();
    expect(screen.getByRole('progressbar', { name: 'Chapter completion' })).toHaveAttribute(
      'title',
      'Calculated from chapter statuses in the chapter list.',
    );
  });

  it('Overview does not turn failed lists into an empty world when dashboard also fails', async () => {
    mockResources({
      dashboardFails: true,
      empty: true,
      failedLists: ['agents', 'chapters', 'scenes', 'files'],
    });
    render(<OverviewPage worldId="world-1" sessions={[]} onNavigate={navigate} />);

    expect(await screen.findByText(/Some overview data is unavailable\./)).toBeDefined();
    expect(
      screen.queryByRole('heading', { name: 'Your world is ready for its first details' }),
    ).toBeNull();
    expect(screen.getByText('Characters')).toBeDefined();
    expect(screen.queryByText('Chapters')).toBeNull();
    expect(screen.queryByText('Scenes')).toBeNull();
    expect(screen.queryByText('Files')).toBeNull();
    expect(screen.getByText('Reminder sources are unavailable.')).toBeDefined();
  });

  it('Overview omits only a failed metric and shows a partial warning', async () => {
    mockResources({ failedLists: ['files'] });
    vi.mocked(worldbuildingApi.getDashboard).mockResolvedValue({
      ok: true,
      dashboard: {
        agents: { total: 1 },
        chapters: { total: 1, completed: 0 },
        file_links: { total: 7 },
        progress: { chapter_completion_pct: 0 },
      },
    });
    render(<OverviewPage worldId="world-1" sessions={[]} onNavigate={navigate} />);

    expect(await screen.findByText(/Some overview data is unavailable\./)).toBeDefined();
    expect(screen.getByText('Characters')).toBeDefined();
    expect(screen.getByText('Chapters')).toBeDefined();
    expect(screen.queryByText('Files')).toBeNull();
    expect(
      screen.queryByRole('heading', { name: 'Your world is ready for its first details' }),
    ).toBeNull();
  });

  it('Overview shows an honest empty state for an empty world', async () => {
    mockResources({ empty: true });
    render(<OverviewPage worldId="world-1" sessions={[]} onNavigate={navigate} />);

    expect(
      await screen.findByRole('heading', { name: 'Your world is ready for its first details' }),
    ).toBeDefined();
    expect(screen.queryByText('Recent activity')).toBeNull();
  });
});

describe('World page', () => {
  it('keeps world detail visible when the graph request fails', async () => {
    vi.mocked(api.getWorldDetail).mockResolvedValue({
      ok: true,
      world: {
        id: 'world-1',
        name: 'Starfall City',
        description: 'A city rebuilt after the long night.',
        created_at: '2026-06-01T00:00:00Z',
        stats: { agents: 2, chapters: 1, scenes: 3, open_foreshadowing: 0, active_secrets: 0 },
      },
    });
    vi.mocked(worldbuildingApi.listLocations).mockResolvedValue({
      ok: true,
      locations: [],
      items: [],
    });
    vi.mocked(worldbuildingApi.listFactions).mockResolvedValue({
      ok: true,
      factions: [],
      items: [],
    });
    vi.mocked(worldbuildingApi.listKnowledge).mockResolvedValue({
      ok: true,
      knowledge: [],
      items: [],
    });
    vi.mocked(worldbuildingApi.getTimeline).mockResolvedValue({
      ok: true,
      current_time: { day: 1, period: 1, label: 'First dawn' },
      events: [],
      items: [],
    });
    vi.mocked(worldbuildingApi.listGraphEntities).mockRejectedValue(new Error('Graph unavailable'));

    render(<WorldPage worldId="world-1" />);

    expect(await screen.findByRole('heading', { name: 'Starfall City' })).toBeDefined();
    expect(screen.getByText('A city rebuilt after the long night.')).toBeDefined();
    expect(await screen.findByText('Graph unavailable')).toBeDefined();
    expect(screen.getByRole('button', { name: 'Retry graph' })).toBeDefined();
  });

  it('shows honest empty states for endpoint-backed world sections', async () => {
    vi.mocked(api.getWorldDetail).mockResolvedValue({
      ok: true,
      world: {
        id: 'world-1',
        name: 'Blank World',
        description: '',
        created_at: '2026-06-01T00:00:00Z',
        stats: { agents: 0, chapters: 0, scenes: 0, open_foreshadowing: 0, active_secrets: 0 },
      },
    });
    vi.mocked(worldbuildingApi.listLocations).mockResolvedValue({ ok: true, items: [] });
    vi.mocked(worldbuildingApi.listFactions).mockResolvedValue({ ok: true, items: [] });
    vi.mocked(worldbuildingApi.listKnowledge).mockResolvedValue({ ok: true, items: [] });
    vi.mocked(worldbuildingApi.getTimeline).mockResolvedValue({
      ok: true,
      current_time: { day: 1, period: 1, label: 'Day 1' },
      items: [],
    });
    vi.mocked(worldbuildingApi.listGraphEntities).mockResolvedValue({ ok: true, items: [] });

    render(<WorldPage worldId="world-1" />);

    expect(await screen.findByText('No locations yet.')).toBeDefined();
    expect(screen.getByText('No factions yet.')).toBeDefined();
    expect(screen.getByText('No knowledge records yet.')).toBeDefined();
    expect(screen.getByText('No timeline events yet.')).toBeDefined();
    expect(screen.getByText('No graph entities yet.')).toBeDefined();
  });
});

describe('Characters page', () => {
  const lin = { id: 'lin', name: 'Lin', display_name: 'Lin', kind: 'character' };
  const sora = { id: 'sora', name: 'Sora', display_name: 'Sora', kind: 'character' };

  function detail(id: string, name: string) {
    return {
      ok: true as const,
      agent: {
        id,
        world_id: 'world-1',
        name,
        display_name: name,
        kind: 'character',
        created_at: '2026-06-01T00:00:00Z',
        updated_at: '2026-06-01T00:00:00Z',
        character_card: { version: 1, core_traits: [], identity: `${name} identity` },
      },
    };
  }

  it('loads selected detail once and retains selection after list refresh', async () => {
    vi.mocked(api.listAgents).mockClear();
    vi.mocked(api.fetchAgentDetail).mockClear();
    vi.mocked(api.fetchDiaries).mockClear();
    vi.mocked(api.fetchRelations).mockClear();
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [lin, sora] });
    vi.mocked(api.fetchAgentDetail).mockImplementation((_worldId, agentId) =>
      Promise.resolve(detail(agentId, agentId === 'lin' ? 'Lin' : 'Sora')),
    );
    vi.mocked(api.fetchDiaries).mockResolvedValue({ ok: true, diaries: [] });
    vi.mocked(api.fetchRelations).mockResolvedValue({ ok: true, relations: [] });
    vi.mocked(api.fetchMemorySummaries).mockResolvedValue({ ok: true, summaries: [] });
    vi.mocked(api.fetchAgentVoice).mockResolvedValue({
      ok: true,
      voice: {
        avg_sentence_length: 8,
        sentence_variance: 1,
        question_frequency: 0,
        modifier_ratio: 0,
        sample_count: 2,
        signature_words: [],
        tone_profile: {},
      },
    });

    render(<CharactersPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /Sora/ }));
    expect(await screen.findByRole('heading', { name: 'Sora' })).toBeDefined();
    expect(screen.queryByText('Not set')).toBeNull();
    expect(api.fetchAgentDetail).toHaveBeenCalledTimes(1);

    fireEvent.click(screen.getByRole('button', { name: 'Refresh characters' }));

    await waitFor(() => expect(api.listAgents).toHaveBeenCalledTimes(2));
    expect(screen.getByRole('option', { name: /Sora/ })).toHaveAttribute('aria-selected', 'true');
    expect(api.fetchAgentDetail).toHaveBeenCalledTimes(1);
  });

  it('shows an honest empty state without loading character detail', async () => {
    vi.mocked(api.fetchAgentDetail).mockClear();
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [] });
    render(<CharactersPage worldId="world-1" />);

    expect(await screen.findByRole('heading', { name: 'No characters yet' })).toBeDefined();
    expect(api.fetchAgentDetail).not.toHaveBeenCalled();
  });

  it('renders memory, voice, and real relationship targets independently', async () => {
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [lin, sora] });
    vi.mocked(api.fetchAgentDetail).mockResolvedValue(detail('lin', 'Lin'));
    vi.mocked(api.fetchDiaries).mockResolvedValue({ ok: true, diaries: [] });
    vi.mocked(api.fetchRelations).mockResolvedValue({
      ok: true,
      relations: [
        { agent_id: 'lin', target_id: 'sora', relation_type: 'ally', updated_at: '2026-06-20' },
        {
          agent_id: 'lin',
          target_id: 'missing-agent',
          relation_type: 'rival',
          updated_at: '2026-06-20',
        },
      ],
    });
    vi.mocked(api.fetchMemorySummaries).mockResolvedValue({
      ok: true,
      summaries: [
        {
          id: 'm1',
          period_start: 'Day 1',
          period_end: 'Day 2',
          summary: 'Guarded the gate.',
          source_diary_ids: ['d1'],
          created_at: '2026-06-20',
        },
      ],
    });
    vi.mocked(api.fetchAgentVoice).mockResolvedValue({
      ok: true,
      voice: {
        avg_sentence_length: 9,
        sentence_variance: 2,
        question_frequency: 0.1,
        modifier_ratio: 0.2,
        sample_count: 5,
        signature_words: ['steady'],
        tone_profile: { question_ratio: 0.1 },
      },
    });

    render(<CharactersPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /Lin/ }));

    expect(await screen.findByText('Guarded the gate.')).toBeDefined();
    const relationships = screen.getByRole('heading', { name: 'Relationships' }).closest('section');
    expect(relationships).not.toBeNull();
    expect(within(relationships!).getByText('Sora')).toBeDefined();
    expect(within(relationships!).getByText('missing-agent')).toBeDefined();
    expect(screen.getByText('steady')).toBeDefined();
  });

  it('deletes with confirmation, refreshes the list, and selects a surviving neighbor', async () => {
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    vi.mocked(api.listAgents)
      .mockResolvedValueOnce({ ok: true, agents: [lin, sora] })
      .mockResolvedValueOnce({ ok: true, agents: [lin] });
    vi.mocked(api.fetchAgentDetail).mockImplementation((_worldId, id) =>
      Promise.resolve(detail(id, id === 'lin' ? 'Lin' : 'Sora')),
    );
    vi.mocked(api.fetchDiaries).mockResolvedValue({ ok: true, diaries: [] });
    vi.mocked(api.fetchRelations).mockResolvedValue({ ok: true, relations: [] });
    vi.mocked(api.fetchMemorySummaries).mockResolvedValue({ ok: true, summaries: [] });
    vi.mocked(api.fetchAgentVoice).mockRejectedValue(new Error('Voice fingerprint not found'));
    vi.mocked(api.deleteAgent).mockResolvedValue({ ok: true });

    render(<CharactersPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /Sora/ }));
    await screen.findByRole('heading', { name: 'Sora' });
    fireEvent.click(screen.getByRole('button', { name: 'Delete character' }));

    await waitFor(() => expect(api.deleteAgent).toHaveBeenCalledWith('world-1', 'sora'));
    await waitFor(() =>
      expect(screen.getByRole('option', { name: /Lin/ })).toHaveAttribute('aria-selected', 'true'),
    );
  });

  it('keeps character context accessible below 1050px', () => {
    const css = readFileSync(join(process.cwd(), 'src/pages/CharactersPage.module.css'), 'utf8');
    const responsiveBlock = css.slice(
      css.indexOf('@media (max-width: 1050px)'),
      css.indexOf('@media (max-width: 700px)'),
    );
    expect(responsiveBlock).not.toMatch(/\.contextPane\s*\{[^}]*display:\s*none/s);
    expect(responsiveBlock).toMatch(/\.contextPane\s*\{[^}]*grid-column:\s*1\s*\/\s*-1/s);
  });

  it('refreshes only the character list after creation', async () => {
    vi.mocked(api.listAgents).mockClear();
    vi.mocked(api.fetchAgentDetail).mockClear();
    vi.mocked(api.fetchDiaries).mockClear();
    vi.mocked(api.fetchRelations).mockClear();
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [lin] });
    vi.mocked(api.createAgent).mockResolvedValue({ ok: true, agent_id: 'new-agent', name: 'Mira' });

    render(
      <AppStateProvider>
        <CharactersPage worldId="world-1" />
      </AppStateProvider>,
    );
    fireEvent.click(await screen.findByRole('button', { name: 'Create character' }));
    const dialog = screen.getByRole('dialog', { name: 'Create character' });
    fireEvent.change(within(dialog).getByLabelText('Character name'), {
      target: { value: 'Mira' },
    });
    fireEvent.click(within(dialog).getByRole('button', { name: 'Create character' }));

    await waitFor(() => expect(api.listAgents).toHaveBeenCalledTimes(2));
    expect(api.fetchAgentDetail).not.toHaveBeenCalled();
    expect(api.fetchDiaries).not.toHaveBeenCalled();
    expect(api.fetchRelations).not.toHaveBeenCalled();
  });
});
