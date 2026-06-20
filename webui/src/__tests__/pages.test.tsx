import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { useState } from 'react';
import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { api } from '../api/client';
import { worldbuildingApi } from '../api/worldbuilding';
import { AppStateProvider } from '../AppState';
import CreateForeshadowingModal from '../components/Inspector/CreateForeshadowingModal';
import CreateSecretModal from '../components/Inspector/CreateSecretModal';
import DetailPane from '../components/layout/DetailPane';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import { useResource } from '../hooks/useResource';
import ChaptersPage from '../pages/ChaptersPage';
import CharactersPage from '../pages/CharactersPage';
import OverviewPage from '../pages/OverviewPage';
import ScenesPage from '../pages/ScenesPage';
import ForeshadowingPage, { deriveForeshadowingStatus } from '../pages/ForeshadowingPage';
import SecretsPage from '../pages/SecretsPage';
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
    endScene: vi.fn(),
    readWorkspaceFile: vi.fn(),
    saveWorkspaceFile: vi.fn(),
    patchChapter: vi.fn(),
    listForeshadowing: vi.fn(),
    listSecrets: vi.fn(),
    patchForeshadow: vi.fn(),
    patchSecret: vi.fn(),
    deleteForeshadowing: vi.fn(),
    deleteSecret: vi.fn(),
    createForeshadowing: vi.fn(),
    createSecret: vi.fn(),
  },
}));

describe('Foreshadowing page', () => {
  it('derives overdue only when planned and current chapter positions both exist', () => {
    expect(deriveForeshadowingStatus({ status: 'open', plannedPosition: 2, currentPosition: null })).toBeNull();
    expect(deriveForeshadowingStatus({ status: 'open', plannedPosition: 2, currentPosition: 4 })).toBe('overdue');
    expect(deriveForeshadowingStatus({ status: 'open', plannedPosition: 5, currentPosition: 4 })).toBeNull();
  });

  it('filters real records and keeps selection safe after deletion', async () => {
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    vi.mocked(api.listForeshadowing)
      .mockResolvedValueOnce({
        ok: true,
        items: [
          { id: 'f1', content: 'Silver bell', status: 'open' },
          { id: 'f2', content: 'Broken seal', status: 'paid' },
        ],
      })
      .mockResolvedValueOnce({ ok: true, items: [{ id: 'f2', content: 'Broken seal', status: 'paid' }] });
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: [] });
    vi.mocked(api.listScenes).mockResolvedValue({ ok: true, scenes: [] });
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [] });
    vi.mocked(api.deleteForeshadowing).mockResolvedValue({ ok: true });

    render(<ForeshadowingPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /Silver bell/ }));
    fireEvent.click(screen.getByRole('button', { name: 'Delete foreshadowing' }));

    await waitFor(() => expect(api.deleteForeshadowing).toHaveBeenCalledWith('world-1', 'f1'));
    expect(screen.getByRole('option', { name: /Broken seal/ })).toHaveAttribute('aria-selected', 'true');
  });

  it('selects only visible records when the status filter changes', async () => {
    vi.mocked(api.listForeshadowing).mockResolvedValue({
      ok: true,
      items: [
        { id: 'f1', content: 'Silver bell', status: 'open' },
        { id: 'f2', content: 'Broken seal', status: 'paid' },
      ],
    });
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: [] });
    vi.mocked(api.listScenes).mockResolvedValue({ ok: true, scenes: [] });
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [] });
    render(<ForeshadowingPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /Silver bell/ }));

    fireEvent.change(screen.getByLabelText('Status'), { target: { value: 'paid' } });

    expect(screen.getByRole('option', { name: /Broken seal/ })).toHaveAttribute('aria-selected', 'true');
    expect(screen.getByRole('heading', { name: 'Broken seal' })).toBeDefined();
  });

  it('resolves real planted and paid narrative IDs and retains the list when context fails', async () => {
    vi.mocked(api.listForeshadowing).mockResolvedValue({
      ok: true,
      items: [{ id: 'f1', content: 'Silver bell', status: 'paid', planted_at: 'scene-1', paid_at: 'chapter-2' }],
    });
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: [{ id: 'chapter-2', title: 'Reckoning', status: 'draft' }] });
    vi.mocked(api.listScenes).mockResolvedValue({ ok: true, scenes: [{ id: 'scene-1', title: 'The warning', status: 'planned', chapter_id: 'chapter-1' }] });
    render(<ForeshadowingPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /Silver bell/ }));
    expect(screen.getByText('The warning')).toBeDefined();
    expect(screen.getByText('Reckoning')).toBeDefined();

    vi.mocked(api.listChapters).mockRejectedValue(new Error('Chapters unavailable'));
    vi.mocked(api.listScenes).mockRejectedValue(new Error('Scenes unavailable'));
    fireEvent.click(screen.getByRole('button', { name: 'Refresh foreshadowing' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('Chapters unavailable');
    expect(screen.getByRole('option', { name: /Silver bell/ })).toBeDefined();
  });

  it('ignores a completed delete after switching worlds and disables duplicate mutation', async () => {
    const deletion = deferred<{ ok: boolean }>();
    vi.mocked(api.deleteForeshadowing).mockClear();
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    vi.mocked(api.listForeshadowing).mockImplementation((worldId) => Promise.resolve({
      ok: true,
      items: [{ id: 'shared', content: worldId === 'world-1' ? 'Old thread' : 'New thread', status: 'open' }],
    }));
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: [] });
    vi.mocked(api.listScenes).mockResolvedValue({ ok: true, scenes: [] });
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [] });
    vi.mocked(api.deleteForeshadowing).mockReturnValue(deletion.promise);
    const view = render(<ForeshadowingPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /Old thread/ }));
    const deleteButton = screen.getByRole('button', { name: 'Delete foreshadowing' });
    fireEvent.click(deleteButton);
    expect(deleteButton).toBeDisabled();
    fireEvent.click(deleteButton);
    expect(api.deleteForeshadowing).toHaveBeenCalledTimes(1);

    view.rerender(<ForeshadowingPage worldId="world-2" />);
    expect(await screen.findByRole('option', { name: /New thread/ })).toBeDefined();
    await act(async () => deletion.resolve({ ok: true }));
    expect(screen.getByRole('option', { name: /New thread/ })).toBeDefined();
  });

  it('does not let an A-B-A delete completion clear the new world generation lock', async () => {
    const oldDeletion = deferred<{ ok: boolean }>();
    const newDeletion = deferred<{ ok: boolean }>();
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    vi.mocked(api.listForeshadowing).mockImplementation((worldId) => Promise.resolve({
      ok: true,
      items: [{ id: 'shared', content: worldId === 'world-a' ? 'A thread' : 'B thread', status: 'open' }],
    }));
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: [] });
    vi.mocked(api.listScenes).mockResolvedValue({ ok: true, scenes: [] });
    vi.mocked(api.deleteForeshadowing).mockReset().mockReturnValueOnce(oldDeletion.promise).mockReturnValueOnce(newDeletion.promise);
    const view = render(<ForeshadowingPage worldId="world-a" />);
    fireEvent.click(await screen.findByRole('option', { name: /A thread/ }));
    fireEvent.click(screen.getByRole('button', { name: 'Delete foreshadowing' }));
    view.rerender(<ForeshadowingPage worldId="world-b" />);
    await screen.findByRole('option', { name: /B thread/ });
    view.rerender(<ForeshadowingPage worldId="world-a" />);
    fireEvent.click(await screen.findByRole('option', { name: /A thread/ }));
    fireEvent.click(screen.getByRole('button', { name: 'Delete foreshadowing' }));

    await act(async () => oldDeletion.resolve({ ok: true }));

    expect(screen.getByRole('button', { name: 'Delete foreshadowing' })).toBeDisabled();
    expect(screen.getByRole('option', { name: /A thread/ })).toBeDefined();
    await act(async () => newDeletion.resolve({ ok: true }));
  });

  it('closes the create mutation when the world changes', async () => {
    vi.mocked(api.listForeshadowing).mockResolvedValue({ ok: true, items: [] });
    const view = render(
      <AppStateProvider>
        <ForeshadowingPage worldId="world-1" />
      </AppStateProvider>,
    );
    await screen.findByText('No foreshadowing matches this filter.');
    fireEvent.click(screen.getByRole('button', { name: 'Create foreshadowing' }));
    expect(screen.getByRole('dialog')).toBeDefined();

    view.rerender(
      <AppStateProvider>
        <ForeshadowingPage worldId="world-2" />
      </AppStateProvider>,
    );

    expect(screen.queryByRole('dialog')).toBeNull();
  });

  it('does not let an A-B-A secret delete completion clear the new generation lock', async () => {
    const oldDeletion = deferred<{ ok: boolean }>();
    const newDeletion = deferred<{ ok: boolean }>();
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    vi.mocked(api.listSecrets).mockImplementation((worldId) => Promise.resolve({ ok: true, items: [{ id: 'shared', title: `${worldId} secret`, status: 'active' }] }));
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [] });
    vi.mocked(api.deleteSecret).mockReset().mockReturnValueOnce(oldDeletion.promise).mockReturnValueOnce(newDeletion.promise);
    const view = render(<SecretsPage worldId="world-a" />);
    fireEvent.click(await screen.findByRole('option', { name: /world-a secret/ }));
    fireEvent.click(screen.getByRole('button', { name: 'Delete secret' }));
    view.rerender(<SecretsPage worldId="world-b" />);
    await screen.findByRole('option', { name: /world-b secret/ });
    view.rerender(<SecretsPage worldId="world-a" />);
    fireEvent.click(await screen.findByRole('option', { name: /world-a secret/ }));
    fireEvent.click(screen.getByRole('button', { name: 'Delete secret' }));

    await act(async () => oldDeletion.resolve({ ok: true }));

    expect(screen.getByRole('button', { name: 'Delete secret' })).toBeDisabled();
    expect(screen.getByRole('option', { name: /world-a secret/ })).toBeDefined();
    await act(async () => newDeletion.resolve({ ok: true }));
  });
});

describe('Task 10 create mutation lifetime', () => {
  it.each([
    ['foreshadowing', CreateForeshadowingModal, 'createForeshadowing'],
    ['secret', CreateSecretModal, 'createSecret'],
  ] as const)('ignores %s callbacks after unmount', async (_name, Modal, method) => {
    const creation = deferred<{ ok: boolean }>();
    const onCreated = vi.fn();
    const onClose = vi.fn();
    vi.mocked(api[method]).mockReturnValue(creation.promise as never);
    const view = render(<AppStateProvider><Modal worldId="world-a" onCreated={onCreated} onClose={onClose} /></AppStateProvider>);
    const field = view.container.querySelector('textarea, input') as HTMLInputElement | HTMLTextAreaElement;
    fireEvent.change(field, { target: { value: 'Required value' } });
    const submit = Array.from(view.container.querySelectorAll('button')).at(-1) as HTMLButtonElement;
    fireEvent.click(submit);
    view.unmount();

    await act(async () => creation.resolve({ ok: true }));

    expect(onCreated).not.toHaveBeenCalled();
    expect(onClose).not.toHaveBeenCalled();
  });

  it.each([
    ['foreshadowing', CreateForeshadowingModal, 'createForeshadowing'],
    ['secret', CreateSecretModal, 'createSecret'],
  ] as const)('ignores stale %s callbacks after an A-B-A world change', async (_name, Modal, method) => {
    const creation = deferred<{ ok: boolean }>();
    const onCreated = vi.fn();
    const onClose = vi.fn();
    vi.mocked(api[method]).mockReturnValue(creation.promise as never);
    const renderModal = (worldId: string) => <AppStateProvider><Modal worldId={worldId} onCreated={onCreated} onClose={onClose} /></AppStateProvider>;
    const view = render(renderModal('world-a'));
    const field = view.container.querySelector('textarea, input') as HTMLInputElement | HTMLTextAreaElement;
    fireEvent.change(field, { target: { value: 'Required value' } });
    fireEvent.click(Array.from(view.container.querySelectorAll('button')).at(-1) as HTMLButtonElement);
    view.rerender(renderModal('world-b'));
    view.rerender(renderModal('world-a'));

    await act(async () => creation.resolve({ ok: true }));

    expect(onCreated).not.toHaveBeenCalled();
    expect(onClose).not.toHaveBeenCalled();
  });
});

describe('Secrets page', () => {
  const secrets = {
    ok: true,
    items: [
      { id: 's1', title: 'The pact', truth: 'The crown is counterfeit', status: 'active', aware_character_ids: ['a1', 'missing'] },
      { id: 's2', title: 'The oath', truth: 'The oath was staged', status: 'exposed' },
    ],
  };

  it('keeps truth absent from the DOM until explicit reveal and hides it on selection and world changes', async () => {
    vi.mocked(api.listSecrets).mockResolvedValue(secrets);
    vi.mocked(api.listForeshadowing).mockResolvedValue({ ok: true, items: [] });
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: [] });
    vi.mocked(api.listScenes).mockResolvedValue({ ok: true, scenes: [] });
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [{ id: 'a1', name: 'Mira', display_name: 'Mira', kind: 'character' }] });

    const view = render(<SecretsPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /The pact/ }));
    expect(screen.queryByText('The crown is counterfeit')).toBeNull();
    expect(screen.queryByText('missing')).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Reveal truth' }));
    expect(screen.getByText('The crown is counterfeit')).toBeDefined();
    fireEvent.click(screen.getByRole('option', { name: /The oath/ }));
    expect(screen.queryByText('The crown is counterfeit')).toBeNull();
    expect(screen.queryByText('The oath was staged')).toBeNull();
    view.rerender(<SecretsPage worldId="world-2" />);
    expect(screen.queryByText('The oath was staged')).toBeNull();
  });

  it('retains the secret list when relationship context is partially unavailable', async () => {
    vi.mocked(api.listSecrets).mockResolvedValue(secrets);
    vi.mocked(api.listForeshadowing).mockResolvedValue({ ok: true, items: [] });
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: [] });
    vi.mocked(api.listScenes).mockResolvedValue({ ok: true, scenes: [] });
    vi.mocked(api.listAgents).mockRejectedValue(new Error('Characters unavailable'));

    render(<SecretsPage worldId="world-1" />);

    expect(await screen.findByRole('option', { name: /The pact/ })).toBeDefined();
    expect(screen.getByRole('alert')).toHaveTextContent('Characters unavailable');
  });

  it('uses the exposed status and selects the first visible filtered secret', async () => {
    vi.mocked(api.listSecrets).mockResolvedValue(secrets);
    vi.mocked(api.listForeshadowing).mockResolvedValue({ ok: true, items: [] });
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: [] });
    vi.mocked(api.listScenes).mockResolvedValue({ ok: true, scenes: [] });
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [] });
    render(<SecretsPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /The pact/ }));

    fireEvent.change(screen.getByLabelText('Status'), { target: { value: 'exposed' } });

    expect(screen.getByRole('option', { name: /The oath/ })).toHaveAttribute('aria-selected', 'true');
    expect(screen.getByRole('heading', { name: 'The oath' })).toBeDefined();
  });

  it('keeps revealed truth scoped to its secret across rerenders', async () => {
    vi.mocked(api.listSecrets).mockResolvedValue(secrets);
    vi.mocked(api.listForeshadowing).mockResolvedValue({ ok: true, items: [] });
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: [] });
    vi.mocked(api.listScenes).mockResolvedValue({ ok: true, scenes: [] });
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [] });
    const view = render(<SecretsPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /The pact/ }));
    fireEvent.click(screen.getByRole('button', { name: 'Reveal truth' }));
    expect(screen.getByText('The crown is counterfeit')).toBeDefined();

    view.rerender(<SecretsPage worldId="world-2" />);

    expect(screen.queryByText('The crown is counterfeit')).toBeNull();
    expect(screen.queryByText('The oath was staged')).toBeNull();
  });

  it('closes the create mutation when the world changes', async () => {
    vi.mocked(api.listSecrets).mockResolvedValue({ ok: true, items: [] });
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [] });
    const view = render(
      <AppStateProvider>
        <SecretsPage worldId="world-1" />
      </AppStateProvider>,
    );
    await screen.findByText('No secrets match this filter.');
    fireEvent.click(screen.getByRole('button', { name: 'Create secret' }));
    expect(screen.getByRole('dialog')).toBeDefined();

    view.rerender(
      <AppStateProvider>
        <SecretsPage worldId="world-2" />
      </AppStateProvider>,
    );

    expect(screen.queryByRole('dialog')).toBeNull();
  });
});

describe('Foreshadowing and Secrets responsive layout', () => {
  it.each(['ForeshadowingPage.module.css', 'SecretsPage.module.css'])('%s owns scrolling when context stacks', (file) => {
    const css = readFileSync(join(process.cwd(), 'src/pages', file), 'utf8');
    const responsive = css.slice(css.indexOf('@media (max-width: 1100px)'), css.indexOf('@media (max-width: 700px)'));
    expect(css).toMatch(/\.workspace\s*\{[^}]*height:\s*100%[^}]*min-height:\s*0[^}]*overflow:\s*auto/s);
    expect(responsive).toMatch(/\.workspace\s*\{[^}]*align-content:\s*start/s);
    expect(responsive).not.toMatch(/\.contextPane\s*\{[^}]*display:\s*none/s);
  });
});

vi.mock('../api/worldbuilding', () => ({
  worldbuildingApi: {
    getDashboard: vi.fn(),
    listLocations: vi.fn(),
    listKnowledge: vi.fn(),
    listFactions: vi.fn(),
    getTimeline: vi.fn(),
    listGraphEntities: vi.fn(),
    reorderChapters: vi.fn(),
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

const chapterFixtures = [
  {
    id: 'chapter-1',
    title: 'Ashes at Dawn',
    number: 1,
    status: 'completed',
    scene_count: 2,
    updated_at: '2026-06-18T08:00:00Z',
  },
  {
    id: 'chapter-2',
    title: 'The Rain Archive',
    number: 2,
    status: 'draft',
    scene_count: 1,
    updated_at: '2026-06-19T08:00:00Z',
  },
];

const sceneFixtures = [
  {
    id: 'scene-1',
    title: 'Crossing the flooded stacks',
    chapter_id: 'chapter-2',
    world_time: 'Day 4, dusk',
    status: 'writing',
    participant_ids: ['lin', 'sora'],
    updated_at: '2026-06-19T09:00:00Z',
  },
];

describe('Chapter and scene pages', () => {
  it('moves chapters with keyboard-accessible controls and sends the exact ordered IDs', async () => {
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: chapterFixtures });
    vi.mocked(worldbuildingApi.reorderChapters).mockResolvedValue({ ok: true });

    render(
      <AppStateProvider>
        <ChaptersPage worldId="world-1" />
      </AppStateProvider>,
    );
    fireEvent.click(await screen.findByRole('button', { name: 'Move The Rain Archive previous' }));

    await waitFor(() =>
      expect(worldbuildingApi.reorderChapters).toHaveBeenCalledWith('world-1', [
        'chapter-2',
        'chapter-1',
      ]),
    );
    expect(
      screen.getAllByRole('heading', { level: 2 }).map((heading) => heading.textContent),
    ).toEqual(['The Rain Archive', 'Ashes at Dawn']);
  });

  it('disables chapter moves while a reorder request is pending', async () => {
    const reorder = deferred<{ ok: boolean }>();
    vi.mocked(worldbuildingApi.reorderChapters).mockClear();
    vi.mocked(api.listChapters).mockClear();
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: chapterFixtures });
    vi.mocked(worldbuildingApi.reorderChapters).mockReturnValue(reorder.promise);

    render(
      <AppStateProvider>
        <ChaptersPage worldId="world-1" />
      </AppStateProvider>,
    );
    const movePrevious = await screen.findByRole('button', {
      name: 'Move The Rain Archive previous',
    });
    fireEvent.click(movePrevious);

    const moveNext = screen.getByRole('button', { name: 'Move The Rain Archive next' });
    const refresh = screen.getByRole('button', { name: 'Refresh' });
    expect(moveNext).toBeDisabled();
    expect(refresh).toBeDisabled();
    expect(screen.getAllByRole('heading', { level: 2 }).map((item) => item.textContent)).toEqual([
      'The Rain Archive',
      'Ashes at Dawn',
    ]);
    fireEvent.click(refresh);
    fireEvent.click(moveNext);
    expect(worldbuildingApi.reorderChapters).toHaveBeenCalledTimes(1);
    expect(api.listChapters).toHaveBeenCalledTimes(1);

    await act(async () => reorder.resolve({ ok: true }));
    await waitFor(() => expect(moveNext).not.toBeDisabled());
    expect(refresh).not.toBeDisabled();
  });

  it('reconciles local chapter order with a fresh server response', async () => {
    vi.mocked(api.listChapters)
      .mockResolvedValueOnce({ ok: true, chapters: chapterFixtures })
      .mockResolvedValueOnce({
        ok: true,
        chapters: [
          { ...chapterFixtures[1], title: 'Rain Archive Revised', status: 'completed' },
          {
            id: 'chapter-3',
            title: 'A New Signal',
            number: 3,
            status: 'draft',
            scene_count: 0,
            updated_at: '2026-06-20T08:00:00Z',
          },
        ],
      });
    vi.mocked(worldbuildingApi.reorderChapters).mockResolvedValue({ ok: true });

    render(
      <AppStateProvider>
        <ChaptersPage worldId="world-1" />
      </AppStateProvider>,
    );
    fireEvent.click(await screen.findByRole('button', { name: 'Move The Rain Archive previous' }));
    const refresh = screen.getByRole('button', { name: 'Refresh' });
    await waitFor(() => expect(refresh).not.toBeDisabled());
    fireEvent.click(refresh);

    expect(await screen.findByRole('heading', { name: 'Rain Archive Revised' })).toBeDefined();
    expect(screen.getByRole('heading', { name: 'A New Signal' })).toBeDefined();
    expect(screen.queryByRole('heading', { name: 'Ashes at Dawn' })).toBeNull();
  });

  it.each([
    ['Chapters', ChaptersPage, 'Loading chapters', 'No chapters yet', 'Chapter list unavailable'],
    ['Scenes', ScenesPage, 'Loading scenes', 'No scenes yet', 'Scene list unavailable'],
  ] as const)(
    'renders %s loading, empty, and error states',
    async (_name, Page, loading, empty, error) => {
      const list = _name === 'Chapters' ? api.listChapters : api.listScenes;
      const pending = deferred<never>();
      vi.mocked(list as typeof api.listChapters).mockReturnValueOnce(pending.promise);
      const { unmount } = render(
        <AppStateProvider>
          <Page worldId="world-1" />
        </AppStateProvider>,
      );
      expect(screen.getByRole('status', { name: loading })).toBeDefined();
      unmount();

      if (_name === 'Chapters') {
        vi.mocked(api.listChapters).mockResolvedValueOnce({ ok: true, chapters: [] });
      } else {
        vi.mocked(api.listScenes).mockResolvedValueOnce({ ok: true, scenes: [] });
      }
      const emptyRender = render(
        <AppStateProvider>
          <Page worldId="world-1" />
        </AppStateProvider>,
      );
      expect(await screen.findByRole('heading', { name: empty })).toBeDefined();
      emptyRender.unmount();

      vi.mocked(list as typeof api.listChapters).mockRejectedValueOnce(new Error(error));
      render(
        <AppStateProvider>
          <Page worldId="world-1" />
        </AppStateProvider>,
      );
      expect(await screen.findByRole('alert')).toHaveTextContent(error);
    },
  );

  it('opens the selected chapter in the reusable editor', async () => {
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: chapterFixtures });
    vi.mocked(api.readWorkspaceFile).mockResolvedValue({
      ok: true,
      file: {
        path: 'chapters/world-1/chapter-2.md',
        content: 'Rain pressed against the archive glass.',
        encoding: 'utf-8',
        updated_at: '2026-06-19T09:00:00Z',
        version: 'v1',
      },
    });

    render(
      <AppStateProvider>
        <ChaptersPage worldId="world-1" />
      </AppStateProvider>,
    );
    fireEvent.click(await screen.findByRole('button', { name: 'Edit The Rain Archive' }));

    expect(await screen.findByRole('textbox', { name: 'Chapter content' })).toHaveValue(
      'Rain pressed against the archive glass.',
    );
  });

  it('keeps chapter editor dirty state and sends the loaded version when saving', async () => {
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: chapterFixtures });
    vi.mocked(api.readWorkspaceFile).mockResolvedValue({
      ok: true,
      file: {
        path: 'chapters/world-1/chapter-2.md',
        content: 'Original draft.',
        encoding: 'utf-8',
        updated_at: '2026-06-19T09:00:00Z',
        version: 'v7',
      },
    });
    vi.mocked(api.saveWorkspaceFile).mockRejectedValue(new Error('Conflict: file changed on disk'));

    render(
      <AppStateProvider>
        <ChaptersPage worldId="world-1" />
      </AppStateProvider>,
    );
    fireEvent.click(await screen.findByRole('button', { name: 'Edit The Rain Archive' }));
    const editor = await screen.findByRole('textbox', { name: 'Chapter content' });
    fireEvent.change(editor, { target: { value: 'Revised draft.' } });
    expect(screen.getByText('Unsaved changes')).toBeDefined();
    fireEvent.click(screen.getByRole('button', { name: 'Save chapter' }));

    await waitFor(() =>
      expect(api.saveWorkspaceFile).toHaveBeenCalledWith(
        'chapters/world-1/chapter-2.md',
        'Revised draft.',
        'v7',
      ),
    );
    expect(await screen.findByRole('alert')).toHaveTextContent('Conflict: file changed on disk');
    expect(screen.getByText('Unsaved changes')).toBeDefined();
  });

  it('keeps the dirty chapter selected when switching is cancelled', async () => {
    vi.spyOn(window, 'confirm').mockReturnValue(false);
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: chapterFixtures });
    vi.mocked(api.readWorkspaceFile).mockResolvedValue({
      ok: true,
      file: {
        path: 'chapters/world-1/chapter-2.md',
        content: 'Original draft.',
        encoding: 'utf-8',
        updated_at: '2026-06-19T09:00:00Z',
        version: 'v7',
      },
    });

    render(
      <AppStateProvider>
        <ChaptersPage worldId="world-1" />
      </AppStateProvider>,
    );
    fireEvent.click(await screen.findByRole('button', { name: 'Edit The Rain Archive' }));
    const editor = await screen.findByRole('textbox', { name: 'Chapter content' });
    fireEvent.change(editor, { target: { value: 'Unsaved rain.' } });
    fireEvent.click(screen.getByRole('button', { name: 'Edit Ashes at Dawn' }));

    expect(window.confirm).toHaveBeenCalledTimes(1);
    expect(screen.getByRole('region', { name: 'Editing The Rain Archive' })).toBeDefined();
    expect(editor).toHaveValue('Unsaved rain.');
  });

  it('blocks chapter switching without confirmation while a save is pending', async () => {
    const saving = deferred<Awaited<ReturnType<typeof api.saveWorkspaceFile>>>();
    const confirm = vi.spyOn(window, 'confirm');
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: chapterFixtures });
    vi.mocked(api.readWorkspaceFile).mockResolvedValue({
      ok: true,
      file: {
        path: 'chapters/world-1/chapter-2.md',
        content: 'Original draft.',
        encoding: 'utf-8',
        updated_at: '2026-06-19T09:00:00Z',
        version: 'v7',
      },
    });
    vi.mocked(api.saveWorkspaceFile).mockReturnValue(saving.promise);

    render(
      <AppStateProvider>
        <ChaptersPage worldId="world-1" />
      </AppStateProvider>,
    );
    fireEvent.click(await screen.findByRole('button', { name: 'Edit The Rain Archive' }));
    const editor = await screen.findByRole('textbox', { name: 'Chapter content' });
    fireEvent.change(editor, { target: { value: 'Saving rain.' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save chapter' }));
    expect(screen.getByRole('button', { name: 'Save chapter' })).toHaveTextContent('Saving...');
    fireEvent.click(screen.getByRole('button', { name: 'Edit Ashes at Dawn' }));

    expect(confirm).not.toHaveBeenCalled();
    expect(screen.getByRole('region', { name: 'Editing The Rain Archive' })).toBeDefined();
    expect(editor).toHaveValue('Saving rain.');

    await act(async () =>
      saving.resolve({
        ok: true,
        file: { path: 'chapters/world-1/chapter-2.md', updated_at: 'now', version: 'v8' },
      }),
    );
  });

  it('commits a successful file version before reporting a title metadata failure', async () => {
    vi.mocked(api.saveWorkspaceFile).mockClear();
    vi.mocked(api.listChapters).mockResolvedValue({ ok: true, chapters: chapterFixtures });
    vi.mocked(api.readWorkspaceFile).mockResolvedValue({
      ok: true,
      file: {
        path: 'chapters/world-1/chapter-2.md',
        content: 'Original draft.',
        encoding: 'utf-8',
        updated_at: '2026-06-19T09:00:00Z',
        version: 'v7',
      },
    });
    vi.mocked(api.saveWorkspaceFile)
      .mockResolvedValueOnce({
        ok: true,
        file: { path: 'chapters/world-1/chapter-2.md', updated_at: 'now', version: 'v8' },
      })
      .mockResolvedValueOnce({
        ok: true,
        file: { path: 'chapters/world-1/chapter-2.md', updated_at: 'later', version: 'v9' },
      });
    vi.mocked(api.patchChapter)
      .mockRejectedValueOnce(new Error('Title service unavailable'))
      .mockResolvedValueOnce({ ok: true });

    render(
      <AppStateProvider>
        <ChaptersPage worldId="world-1" />
      </AppStateProvider>,
    );
    fireEvent.click(await screen.findByRole('button', { name: 'Edit The Rain Archive' }));
    fireEvent.change(await screen.findByRole('textbox', { name: 'Chapter content' }), {
      target: { value: 'Saved prose.' },
    });
    fireEvent.change(screen.getByRole('textbox', { name: 'Chapter title' }), {
      target: { value: 'New title' },
    });
    fireEvent.click(screen.getByRole('button', { name: 'Save chapter' }));

    expect(await screen.findByRole('alert')).toHaveTextContent(
      'Chapter text saved, but title update failed: Title service unavailable',
    );
    fireEvent.click(screen.getByRole('button', { name: 'Save chapter' }));
    await waitFor(() => expect(api.saveWorkspaceFile).toHaveBeenCalledTimes(2));
    expect(api.saveWorkspaceFile).toHaveBeenLastCalledWith(
      'chapters/world-1/chapter-2.md',
      'Saved prose.',
      'v8',
    );
  });

  it('renders only actual scene fields and omits unsupported consistency content', async () => {
    vi.mocked(api.listScenes).mockResolvedValue({ ok: true, scenes: sceneFixtures });
    render(<ScenesPage worldId="world-1" />);

    fireEvent.click(await screen.findByRole('option', { name: /Crossing the flooded stacks/ }));
    expect(screen.getByText('Day 4, dusk')).toBeDefined();
    expect(screen.getByText('lin, sora')).toBeDefined();
    expect(screen.queryByText(/consistency/i)).toBeNull();
  });

  it('lets the chapter editor toolbar wrap at narrow widths without clipping actions', () => {
    const css = readFileSync(
      join(process.cwd(), 'src/components/ChapterEditor.module.css'),
      'utf8',
    );
    expect(css).toMatch(/\.container\s*\{[^}]*min-width:\s*0/s);
    expect(css).toMatch(/\.toolbar\s*\{[^}]*flex-wrap:\s*wrap/s);
    expect(css).toMatch(/\.toolbarRight\s*\{[^}]*overflow-x:\s*auto/s);
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

  it('filters a confirmed deletion when retained list refresh fails', async () => {
    const deletion = deferred<{ ok: boolean }>();
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    vi.mocked(api.listAgents)
      .mockResolvedValueOnce({ ok: true, agents: [lin, sora] })
      .mockRejectedValueOnce(new Error('List refresh failed'));
    vi.mocked(api.fetchAgentDetail).mockImplementation((_worldId, id) =>
      Promise.resolve(detail(id, id === 'lin' ? 'Lin' : 'Sora')),
    );
    vi.mocked(api.fetchDiaries).mockResolvedValue({ ok: true, diaries: [] });
    vi.mocked(api.fetchRelations).mockResolvedValue({ ok: true, relations: [] });
    vi.mocked(api.fetchMemorySummaries).mockResolvedValue({ ok: true, summaries: [] });
    vi.mocked(api.fetchAgentVoice).mockResolvedValue({ ok: true, voice: null });
    vi.mocked(api.deleteAgent).mockReturnValue(deletion.promise);

    render(<CharactersPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /Sora/ }));
    await screen.findByRole('heading', { name: 'Sora' });
    fireEvent.click(screen.getByRole('button', { name: 'Delete character' }));
    expect(screen.getByRole('option', { name: /Sora/ })).toBeDefined();

    await act(async () => deletion.resolve({ ok: true }));

    await waitFor(() => expect(screen.queryByRole('option', { name: /Sora/ })).toBeNull());
    expect(await screen.findByRole('alert')).toHaveTextContent('List refresh failed');
    expect(screen.getByRole('button', { name: 'Retry character list' })).toBeDefined();
    expect(screen.getByRole('option', { name: /Lin/ })).toHaveAttribute('aria-selected', 'true');
  });

  it('shows retained refresh failure beside empty state after deleting the only character', async () => {
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    vi.mocked(api.listAgents)
      .mockResolvedValueOnce({ ok: true, agents: [lin] })
      .mockRejectedValueOnce(new Error('List refresh failed'));
    vi.mocked(api.fetchAgentDetail).mockResolvedValue(detail('lin', 'Lin'));
    vi.mocked(api.fetchDiaries).mockResolvedValue({ ok: true, diaries: [] });
    vi.mocked(api.fetchRelations).mockResolvedValue({ ok: true, relations: [] });
    vi.mocked(api.fetchMemorySummaries).mockResolvedValue({ ok: true, summaries: [] });
    vi.mocked(api.fetchAgentVoice).mockResolvedValue({ ok: true, voice: null });
    vi.mocked(api.deleteAgent).mockResolvedValue({ ok: true });

    render(<CharactersPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /Lin/ }));
    await screen.findByRole('heading', { name: 'Lin' });
    fireEvent.click(screen.getByRole('button', { name: 'Delete character' }));

    expect(await screen.findByRole('heading', { name: 'No characters yet' })).toBeDefined();
    expect(screen.queryByRole('option', { name: /Lin/ })).toBeNull();
    expect(await screen.findByRole('alert')).toHaveTextContent('List refresh failed');
    expect(screen.getByRole('button', { name: 'Retry character list' })).toBeDefined();
  });

  it('renders voice 404 as empty and voice 500 as retryable error', async () => {
    vi.mocked(api.listAgents).mockResolvedValue({ ok: true, agents: [lin, sora] });
    vi.mocked(api.fetchAgentDetail).mockImplementation((_worldId, id) =>
      Promise.resolve(detail(id, id === 'lin' ? 'Lin' : 'Sora')),
    );
    vi.mocked(api.fetchDiaries).mockResolvedValue({ ok: true, diaries: [] });
    vi.mocked(api.fetchRelations).mockResolvedValue({ ok: true, relations: [] });
    vi.mocked(api.fetchMemorySummaries).mockResolvedValue({ ok: true, summaries: [] });
    vi.mocked(api.fetchAgentVoice)
      .mockResolvedValueOnce({ ok: true, voice: null })
      .mockRejectedValueOnce(new Error('Voice store failed'));

    render(<CharactersPage worldId="world-1" />);
    fireEvent.click(await screen.findByRole('option', { name: /Lin/ }));
    expect(await screen.findByText('No voice fingerprint yet.')).toBeDefined();
    fireEvent.click(screen.getByRole('option', { name: /Sora/ }));
    expect(await screen.findByText('Voice store failed')).toBeDefined();
    expect(screen.getByRole('button', { name: 'Retry voice fingerprint' })).toBeDefined();
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
