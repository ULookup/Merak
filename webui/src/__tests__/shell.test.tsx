import { readFileSync } from 'node:fs';
import type { ReactNode } from 'react';
import { fireEvent, render, screen, within } from '@testing-library/react';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { shouldWarnBeforeClose } from '../App';
import { AppStateProvider, useAppState } from '../AppState';
import { useSafeNavigation } from '../hooks/useSafePageNavigation';
import { I18nProvider } from '../i18n';
import DesktopShell from '../shell/DesktopShell';
import { desktopPages, readStoredDesktopPage, writeStoredDesktopPage } from '../shell/navigation';

vi.mock('../DesktopBoot', () => ({
  default: ({ children }: { children: ReactNode }) => children,
}));
vi.mock('../hooks/useSSE', () => ({ useSSE: () => 'connected' }));
vi.mock('../components/WorldSidebar', () => ({ default: () => <aside>Workbench sidebar</aside> }));
vi.mock('../components/MainPanel', () => ({ default: () => <div>Workbench main</div> }));
vi.mock('../components/InspectorPanel', () => ({ default: () => <aside>Inspector</aside> }));
vi.mock('../api/client', () => ({
  api: {
    getConfig: vi.fn().mockResolvedValue({ api_key_masked: '' }),
    metadata: vi.fn().mockResolvedValue({
      provider: 'openai',
      model: 'gpt-4o',
      models: [],
      permission_mode: 'default',
      memory: { enabled: false },
      tools: [],
      mcp_servers: [],
      agents: [],
      delegation_patterns: [],
    }),
    listWorlds: vi.fn().mockResolvedValue({
      worlds: [{ id: 'world-1', name: 'Test World', description: '', created_at: '' }],
    }),
    listSessions: vi.fn().mockResolvedValue({
      sessions: [
        {
          id: 'session-1',
          title: 'Test',
          world_id: 'world-1',
          agent_id: 'agent-1',
          last_seq: 0,
          created_at: '',
          updated_at: '',
          archived_at: null,
        },
      ],
    }),
    capabilities: vi.fn().mockResolvedValue({ capabilities: {}, fallback: false }),
    getPreferences: vi.fn().mockResolvedValue(null),
    getStoryOverview: vi.fn().mockResolvedValue({ overview: null, fallback: false }),
    listAgents: vi.fn().mockResolvedValue({ agents: [] }),
    listForeshadowing: vi.fn().mockResolvedValue({ items: [] }),
    listSecrets: vi.fn().mockResolvedValue({ items: [] }),
    getWorldTime: vi.fn().mockResolvedValue({}),
    sseUrl: vi.fn().mockReturnValue(null),
  },
}));

describe('desktop shell', () => {
  beforeEach(() => window.localStorage.clear());

  function renderShell(page: 'overview' | 'sessions', onNavigate = vi.fn()) {
    return render(
      <I18nProvider defaultLocale="zh">
        <AppStateProvider>
          <DesktopShell page={page} onNavigate={onNavigate}>
            <div>Existing workbench</div>
          </DesktopShell>
        </AppStateProvider>
      </I18nProvider>,
    );
  }

  function SafeNavigationHarness() {
    const { state, dispatch } = useAppState();
    const { requestPageChange } = useSafeNavigation();
    const activeFile = state.workspaceFiles.find((file) => file.id === state.activeEditorFileId);
    return (
      <>
        <button onClick={() => dispatch({ type: 'SET_PAGE', page: 'chapters' })}>
          Start chapters
        </button>
        <button onClick={() => dispatch({ type: 'SET_EDITOR_SAVE_STATUS', status: 'dirty' })}>
          Make dirty
        </button>
        <button onClick={() => dispatch({ type: 'SET_EDITOR_SAVE_STATUS', status: 'saving' })}>
          Start saving
        </button>
        <button
          onClick={() => {
            dispatch({
              type: 'SET_WORLDS',
              worlds: [
                { id: 'world-1', name: 'World One', description: '', created_at: '' },
                { id: 'world-2', name: 'World Two', description: '', created_at: '' },
              ],
            });
            dispatch({ type: 'SET_WORLD', worldId: 'world-1' });
          }}
        >
          Setup worlds
        </button>
        <button
          onClick={() => {
            dispatch({
              type: 'SET_WORKSPACE_FILES',
              files: [
                {
                  id: 'draft.md',
                  path: 'draft.md',
                  name: 'draft.md',
                  ext: '.md',
                  mime: 'text/markdown',
                  size: 8,
                  updated_at: '',
                  dirty: false,
                },
              ],
            });
            dispatch({ type: 'OPEN_WORKSPACE_FILE', fileId: 'draft.md' });
            dispatch({
              type: 'SET_EDITOR_CONTENT',
              fileId: 'draft.md',
              content: {
                path: 'draft.md',
                content: 'Original',
                encoding: 'utf-8',
                updated_at: '',
                version: 'v1',
              },
            });
            dispatch({
              type: 'UPDATE_EDITOR_BUFFER',
              fileId: 'draft.md',
              content: 'Unsaved draft',
            });
          }}
        >
          Open workspace draft
        </button>
        <output aria-label="Current page">{state.currentPage}</output>
        <output aria-label="Editor buffer">
          {state.activeEditorFileId ? state.editorBuffers[state.activeEditorFileId] : ''}
        </output>
        <output aria-label="Editor status">{state.editorSaveStatus}</output>
        <output aria-label="File dirty">{String(activeFile?.dirty ?? false)}</output>
        <output aria-label="Warn before close">{String(shouldWarnBeforeClose(state))}</output>
        <DesktopShell page={state.currentPage} onNavigate={requestPageChange}>
          Embedded chapter editor
        </DesktopShell>
      </>
    );
  }

  function renderSafeNavigation() {
    return render(
      <I18nProvider defaultLocale="en">
        <AppStateProvider>
          <SafeNavigationHarness />
        </AppStateProvider>
      </I18nProvider>,
    );
  }

  it('keeps dirty Chapters navigation on the page when Overview or Scenes is cancelled', () => {
    vi.spyOn(window, 'confirm').mockReturnValue(false);
    renderSafeNavigation();
    fireEvent.click(screen.getByRole('button', { name: 'Start chapters' }));
    fireEvent.click(screen.getByRole('button', { name: 'Make dirty' }));
    const navigation = screen.getByRole('navigation', { name: 'Primary navigation' });

    fireEvent.click(within(navigation).getAllByRole('button')[0]);
    fireEvent.click(within(navigation).getByRole('button', { name: 'Scenes 场景' }));

    expect(screen.getByLabelText('Current page')).toHaveTextContent('chapters');
    expect(window.confirm).toHaveBeenCalledTimes(2);
  });

  it('allows confirmed dirty navigation from Chapters to Scenes', () => {
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    renderSafeNavigation();
    fireEvent.click(screen.getByRole('button', { name: 'Start chapters' }));
    fireEvent.click(screen.getByRole('button', { name: 'Make dirty' }));
    fireEvent.click(screen.getByRole('button', { name: 'Scenes 场景' }));

    expect(screen.getByLabelText('Current page')).toHaveTextContent('scenes');
    fireEvent.click(
      screen.getByRole('navigation', { name: 'Primary navigation' }).querySelectorAll('button')[0],
    );
    expect(window.confirm).toHaveBeenCalledTimes(1);
    expect(screen.getByLabelText('Current page')).toHaveTextContent('overview');
  });

  it('blocks navigation while a chapter save is pending without prompting', () => {
    const confirm = vi.spyOn(window, 'confirm');
    renderSafeNavigation();
    fireEvent.click(screen.getByRole('button', { name: 'Start chapters' }));
    fireEvent.click(screen.getByRole('button', { name: 'Start saving' }));
    fireEvent.click(screen.getByRole('button', { name: 'Scenes 场景' }));

    expect(screen.getByLabelText('Current page')).toHaveTextContent('chapters');
    expect(confirm).not.toHaveBeenCalled();
  });

  it('blocks world changes while saving without prompting', () => {
    const confirm = vi.spyOn(window, 'confirm');
    renderSafeNavigation();
    fireEvent.click(screen.getByRole('button', { name: 'Setup worlds' }));
    fireEvent.click(screen.getByRole('button', { name: 'Start saving' }));

    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'world-2' } });

    expect(screen.getByRole('combobox')).toHaveValue('world-1');
    expect(confirm).not.toHaveBeenCalled();
  });

  it('retains the current world on cancel and changes it after confirmed discard', () => {
    const confirm = vi.spyOn(window, 'confirm').mockReturnValue(false);
    renderSafeNavigation();
    fireEvent.click(screen.getByRole('button', { name: 'Setup worlds' }));
    fireEvent.click(screen.getByRole('button', { name: 'Make dirty' }));

    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'world-2' } });
    expect(screen.getByRole('combobox')).toHaveValue('world-1');
    expect(screen.getByLabelText('Editor status')).toHaveTextContent('dirty');

    confirm.mockReturnValue(true);
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'world-2' } });
    expect(screen.getByRole('combobox')).toHaveValue('world-2');
    expect(screen.getByLabelText('Editor status')).toHaveTextContent('idle');
    expect(confirm).toHaveBeenCalledTimes(2);
  });

  it('preserves a cancelled workspace draft and reverts it after confirmed navigation', () => {
    const confirm = vi.spyOn(window, 'confirm').mockReturnValue(false);
    renderSafeNavigation();
    fireEvent.click(screen.getByRole('button', { name: 'Open workspace draft' }));
    const navigation = screen.getByRole('navigation', { name: 'Primary navigation' });

    fireEvent.click(within(navigation).getAllByRole('button')[5]);
    expect(screen.getByLabelText('Current page')).toHaveTextContent('overview');
    expect(screen.getByLabelText('Editor buffer')).toHaveTextContent('Unsaved draft');
    expect(screen.getByLabelText('File dirty')).toHaveTextContent('true');
    expect(screen.getByLabelText('Warn before close')).toHaveTextContent('true');

    confirm.mockReturnValue(true);
    fireEvent.click(within(navigation).getAllByRole('button')[5]);
    fireEvent.click(within(navigation).getAllByRole('button')[8]);

    expect(screen.getByLabelText('Current page')).toHaveTextContent('files');
    expect(screen.getByLabelText('Editor buffer')).toHaveTextContent('Original');
    expect(screen.getByLabelText('File dirty')).toHaveTextContent('false');
    expect(screen.getByLabelText('Editor status')).toHaveTextContent('idle');
    expect(screen.getByLabelText('Warn before close')).toHaveTextContent('false');
  });

  it('renders exactly ten bilingual navigation buttons and dispatches navigation', () => {
    const dispatch = vi.fn();
    renderShell('overview', (page) => dispatch({ type: 'SET_PAGE', page }));

    const navigation = screen.getByRole('navigation', { name: '主导航' });
    expect(within(navigation).getAllByRole('button')).toHaveLength(10);
    expect(within(navigation).getByRole('button', { name: '概览' })).toBeInTheDocument();
    expect(within(navigation).getByRole('button', { name: 'Sessions 会话' })).toBeInTheDocument();
    expect(within(navigation).getByRole('button', { name: 'World 世界设定' })).toBeInTheDocument();

    fireEvent.click(within(navigation).getByRole('button', { name: /角色/ }));
    expect(dispatch).toHaveBeenCalledWith({ type: 'SET_PAGE', page: 'characters' });
  });

  it('marks the active page and preserves the existing workbench children', () => {
    renderShell('sessions');

    expect(screen.getByRole('button', { name: 'Sessions 会话' })).toHaveAttribute(
      'aria-current',
      'page',
    );
    expect(screen.getByText('Existing workbench')).toBeInTheDocument();
  });

  it('restores only persisted top-level pages', () => {
    window.localStorage.setItem('merak.desktop.page', 'characters');
    expect(readStoredDesktopPage()).toBe('characters');

    window.localStorage.setItem('merak.desktop.page', 'editor');
    expect(readStoredDesktopPage()).toBe('overview');

    window.localStorage.setItem('merak.desktop.page', 'not-a-page');
    expect(readStoredDesktopPage()).toBe('overview');
  });

  it('persists only validated top-level pages', () => {
    writeStoredDesktopPage('files');
    expect(window.localStorage.getItem('merak.desktop.page')).toBe('files');

    writeStoredDesktopPage('editor');
    expect(window.localStorage.getItem('merak.desktop.page')).toBe('files');
    expect(desktopPages).toHaveLength(10);
  });

  it('keeps global overlays outside the constrained page-content outlet', () => {
    render(
      <I18nProvider defaultLocale="zh">
        <AppStateProvider>
          <DesktopShell
            page="overview"
            onNavigate={vi.fn()}
            overlays={<div data-testid="global-overlay">Overlay</div>}
          >
            <div data-testid="page-content">Workbench</div>
          </DesktopShell>
        </AppStateProvider>
      </I18nProvider>,
    );

    const main = screen.getByRole('main');
    expect(within(main).getByTestId('page-content')).toBeInTheDocument();
    expect(within(main).queryByTestId('global-overlay')).toBeNull();
    expect(screen.getByTestId('global-overlay')).toBeInTheDocument();
  });

  it('persists programmatic settings and overview transitions but never editor', () => {
    function PageDispatchHarness() {
      const { dispatch } = useAppState();
      return (
        <>
          <button onClick={() => dispatch({ type: 'SET_PAGE', page: 'settings' })}>Settings</button>
          <button onClick={() => dispatch({ type: 'SET_PAGE', page: 'overview' })}>Overview</button>
          <button onClick={() => dispatch({ type: 'SET_PAGE', page: 'editor' })}>Editor</button>
        </>
      );
    }

    render(
      <AppStateProvider>
        <PageDispatchHarness />
      </AppStateProvider>,
    );

    fireEvent.click(screen.getByRole('button', { name: 'Settings' }));
    expect(window.localStorage.getItem('merak.desktop.page')).toBe('settings');
    fireEvent.click(screen.getByRole('button', { name: 'Overview' }));
    expect(window.localStorage.getItem('merak.desktop.page')).toBe('overview');
    fireEvent.click(screen.getByRole('button', { name: 'Editor' }));
    expect(window.localStorage.getItem('merak.desktop.page')).toBe('overview');
  });

  it('keeps all navigation buttons reachable in short windows', () => {
    const css = readFileSync('src/shell/DesktopShell.module.css', 'utf8');
    expect(css).toMatch(/\.navigation\s*\{[^}]*overflow-y:\s*auto/s);
  });

  it('opens and closes compact navigation with Escape and restores focus', () => {
    renderShell('overview');
    const trigger = screen.getByRole('button', { name: 'Open navigation' });
    fireEvent.click(trigger);
    expect(screen.getByRole('dialog', { name: /navigation|导航/i })).toBeInTheDocument();
    expect(screen.getAllByRole('button', { name: 'Close navigation' })[1]).toHaveFocus();
    fireEvent.keyDown(document, { key: 'Escape' });
    expect(screen.queryByRole('dialog')).toBeNull();
    expect(trigger).toHaveFocus();
  });

  it('traps forward and reverse Tab inside open navigation', () => {
    renderShell('overview');
    fireEvent.click(screen.getByRole('button', { name: 'Open navigation' }));
    const dialog = screen.getByRole('dialog', { name: /navigation|导航/i });
    const controls = within(dialog).getAllByRole('button');
    controls.at(-1)?.focus();
    fireEvent.keyDown(document, { key: 'Tab' });
    expect(controls[0]).toHaveFocus();
    controls[0].focus();
    fireEvent.keyDown(document, { key: 'Tab', shiftKey: true });
    expect(controls.at(-1)).toHaveFocus();
  });

  it('returns focus to the menu trigger after a compact navigation item succeeds', () => {
    const navigate = vi.fn();
    renderShell('overview', navigate);
    const trigger = screen.getByRole('button', { name: 'Open navigation' });
    fireEvent.click(trigger);
    fireEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: /Characters/ }));
    expect(navigate).toHaveBeenCalledWith('characters');
    expect(screen.queryByRole('dialog')).toBeNull();
    expect(trigger).toHaveFocus();
  });

  it('returns focus to the menu trigger when dirty navigation is cancelled', () => {
    vi.spyOn(window, 'confirm').mockReturnValue(false);
    renderSafeNavigation();
    fireEvent.click(screen.getByRole('button', { name: 'Start chapters' }));
    fireEvent.click(screen.getByRole('button', { name: 'Make dirty' }));
    const trigger = screen.getByRole('button', { name: 'Open navigation' });
    fireEvent.click(trigger);
    fireEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: /Scenes/ }));
    expect(screen.getByLabelText('Current page')).toHaveTextContent('chapters');
    expect(screen.queryByRole('dialog')).toBeNull();
    expect(trigger).toHaveFocus();
  });

  it('defines the approved shared visual tokens and responsive shell contracts', () => {
    const globalCss = readFileSync('src/styles/global.css', 'utf8');
    const shellCss = readFileSync('src/shell/DesktopShell.module.css', 'utf8');

    expect(globalCss).toContain('--brand: #06266f');
    expect(globalCss).toContain('--page: #ffffff');
    expect(globalCss).toMatch(/:focus-visible/);
    expect(globalCss).toMatch(/prefers-reduced-motion:\s*reduce/);
    expect(shellCss).toMatch(/@media \(max-width: 1180px\)/);
    expect(shellCss).toMatch(/@media \(max-width: 980px\)/);
  });

  it('gives the world dashboard a definite scroll-container height', () => {
    const css = readFileSync('src/components/WorldDashboard.module.css', 'utf8');
    const dashboardRule = css.match(/\.dashboard\s*\{([^}]*)\}/s)?.[1] ?? '';

    expect(dashboardRule).toMatch(/^\s*height:\s*100%/m);
    expect(dashboardRule).toMatch(/^\s*min-height:\s*0/m);
    expect(dashboardRule).toMatch(/^\s*overflow:\s*auto/m);
  });

  it('mounts App global dialogs outside the constrained page outlet', async () => {
    const { default: App } = await import('../App');
    render(<App />);

    const dialog = await screen.findByRole('dialog', { name: '初始化设置' });
    expect(within(screen.getByRole('main')).queryByRole('dialog')).toBeNull();
    expect(dialog).toBeInTheDocument();
  });
});
