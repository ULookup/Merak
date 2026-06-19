import { readFileSync } from 'node:fs';
import type { ReactNode } from 'react';
import { fireEvent, render, screen, within } from '@testing-library/react';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { AppStateProvider, useAppState } from '../AppState';
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
