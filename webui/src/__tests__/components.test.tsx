import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { StrictMode, useEffect } from 'react';
import { act, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { api } from '../api/client';
import { AppStateProvider, useAppState } from '../AppState';
import BrandMark from '../components/BrandMark';
import AssistantCell from '../components/cells/AssistantCell';
import StatusPill from '../components/cells/StatusPill';
import SystemCell from '../components/cells/SystemCell';
import ToolCell from '../components/cells/ToolCell';
import UserCell from '../components/cells/UserCell';
import ChatTimeline from '../components/ChatTimeline';
import InspectorPanel from '../components/InspectorPanel';
import MainPanel from '../components/MainPanel';
import SessionList from '../components/Sidebar/SessionList';
import WorldSelector from '../components/Sidebar/WorldSelector';
import { ToastProvider } from '../components/Toast';
import WorldDashboard from '../components/WorldDashboard';
import WorldOnboarding from '../components/WorldOnboarding';
import { getDesktopRuntimeSteps } from '../DesktopBoot';

function TimelineHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({
      type: 'SET_METADATA',
      metadata: {
        provider: 'anthropic',
        model: 'claude-sonnet-4-6',
        models: [{ name: 'claude-sonnet-4-6', provider: 'anthropic', max_context_tokens: 200000 }],
        permission_mode: 'default',
        memory: { enabled: true },
        tools: [],
        mcp_servers: [],
        agents: [],
        delegation_patterns: ['fan_out', 'sequential', 'pipeline'],
      },
    });
    dispatch({ type: 'SET_CURRENT_RUN', runId: 'run_1' });
    dispatch({ type: 'SET_STATUS', status: 'thinking' });
    dispatch({ type: 'SET_USAGE', inputTokens: 1200, outputTokens: 80 });
  }, [dispatch]);

  return <ChatTimeline connectionState="connected" />;
}

function StreamingMarkdownHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({ type: 'UPDATE_ASSISTANT', text: '## Plan\n\n' });
    dispatch({ type: 'UPDATE_ASSISTANT', text: '**First** step\n\n```ts\nconst ok = true;\n```' });
  }, [dispatch]);

  return <ChatTimeline />;
}

function SessionIconHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({
      type: 'SET_SESSION',
      sessionId: 'session_1',
    });
    dispatch({
      type: 'SET_SESSIONS',
      sessions: [
        {
          id: 'session_1',
          title: 'Planning scene',
          world_id: null,
          agent_id: null,
          last_seq: 2,
          created_at: '2026-06-06T10:30:00Z',
          updated_at: '2026-06-06T10:35:00Z',
          archived_at: null,
        },
      ],
    });
  }, [dispatch]);

  return <SessionList />;
}

function SessionLifecycleHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({ type: 'SET_SESSION', sessionId: 'session_live' });
    dispatch({
      type: 'SET_SESSIONS',
      sessions: [
        {
          id: 'session_live',
          title: 'Planning scene',
          world_id: null,
          agent_id: null,
          last_seq: 2,
          created_at: '2026-06-06T10:30:00Z',
          updated_at: '2026-06-06T10:35:00Z',
          archived_at: null,
        },
        {
          id: 'session_archived',
          title: 'Old outline',
          world_id: null,
          agent_id: null,
          last_seq: 8,
          created_at: '2026-06-04T09:00:00Z',
          updated_at: '2026-06-04T09:30:00Z',
          archived_at: '2026-06-05T12:00:00Z',
        },
      ],
    });
  }, [dispatch]);

  return <SessionList />;
}

function SessionSelectionHarness() {
  const { state, dispatch } = useAppState();

  useEffect(() => {
    dispatch({ type: 'SET_AGENT_SESSION', sessionId: 'agent_session', agentId: 'agent_old' });
    dispatch({
      type: 'SET_SESSIONS',
      sessions: [
        {
          id: 'agent_session',
          title: 'Agent draft',
          world_id: 'world_1',
          agent_id: 'agent_old',
          last_seq: 1,
          created_at: '2026-06-06T10:30:00Z',
          updated_at: '2026-06-06T10:30:00Z',
          archived_at: null,
        },
        {
          id: 'world_session',
          title: 'World notes',
          world_id: 'world_1',
          agent_id: null,
          last_seq: 2,
          created_at: '2026-06-06T11:00:00Z',
          updated_at: '2026-06-06T11:00:00Z',
          archived_at: null,
        },
      ],
    });
  }, [dispatch]);

  return (
    <>
      <SessionList worldId="world_1" />
      <output aria-label="selected-session">{state.sessionId}</output>
      <output aria-label="selected-agent">{state.agentId ?? 'none'}</output>
    </>
  );
}

function WorldIconHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({
      type: 'SET_WORLDS',
      worlds: [
        {
          id: 'world_1',
          name: 'Northreach',
          description: 'Snowbound border city',
          created_at: '2026-06-06T10:00:00Z',
        },
      ],
    });
    dispatch({ type: 'SET_WORLD', worldId: 'world_1' });
  }, [dispatch]);

  return <WorldSelector />;
}

describe('Cell components', () => {
  it('BrandMark renders the Merak logo accessibly', () => {
    render(<BrandMark />);
    expect(screen.getByRole('img', { name: 'Merak wordmark logo' })).toBeDefined();
    expect(screen.getByText('MERAK')).toBeDefined();
  });

  it('ChatTimeline empty state uses a scene glyph instead of a letter placeholder', () => {
    render(
      <AppStateProvider>
        <ChatTimeline />
      </AppStateProvider>,
    );

    expect(screen.getByRole('img', { name: 'Scene ready mark' })).toBeDefined();
    expect(screen.queryByText('M')).toBeNull();
  });

  it('ChatTimeline shows live agent status outside the message stream', async () => {
    render(
      <AppStateProvider>
        <TimelineHarness />
      </AppStateProvider>,
    );

    expect(await screen.findAllByText('Thinking')).toHaveLength(2);
    expect(screen.getByText('Connected')).toBeDefined();
    expect(screen.getByText('claude-sonnet-4-6')).toBeDefined();
    expect(screen.getByText('1.3K words of context')).toBeDefined();
  });

  it('ChatTimeline renders streamed assistant markdown', async () => {
    render(
      <AppStateProvider>
        <StreamingMarkdownHarness />
      </AppStateProvider>,
    );

    expect(await screen.findByRole('heading', { name: 'Plan' })).toBeDefined();
    expect(screen.getByText('First').tagName.toLowerCase()).toBe('strong');
    expect(
      screen.getByText((_, element) => {
        const normalized = element?.textContent?.replace(/\s+/g, ' ').trim();
        return element?.tagName.toLowerCase() === 'code' && normalized === 'const ok = true;';
      }),
    ).toBeDefined();
  });

  it('MainPanel uses real SVG icons for navigation controls', () => {
    render(
      <AppStateProvider>
        <ToastProvider>
          <MainPanel connectionState="connected" onToggleHistory={() => {}} />
        </ToastProvider>
      </AppStateProvider>,
    );

    expect(
      screen.getByRole('button', { name: 'Open session history' }).querySelector('svg'),
    ).toBeDefined();
    expect(
      screen.getByRole('button', { name: 'Open inspector' }).querySelector('svg'),
    ).toBeDefined();
    expect(screen.queryByText('☰')).toBeNull();
    expect(screen.queryByText('◫')).toBeNull();
  });

  it('MainPanel only references session history controls in sessions mode', () => {
    const { rerender } = render(
      <AppStateProvider>
        <ToastProvider>
          <MainPanel connectionState="connected" onToggleSidebar={() => {}} />
        </ToastProvider>
      </AppStateProvider>,
    );

    expect(screen.getByRole('button', { name: 'Open sidebar' })).not.toHaveAttribute(
      'aria-controls',
    );

    rerender(
      <AppStateProvider>
        <ToastProvider>
          <MainPanel connectionState="connected" onToggleHistory={() => {}} />
        </ToastProvider>
      </AppStateProvider>,
    );

    expect(screen.getByRole('button', { name: 'Open session history' })).toHaveAttribute(
      'aria-controls',
      'session-history-panel',
    );
  });

  it('keeps the sessions center column shrinkable before inspector overlay mode', () => {
    const css = readFileSync(join(process.cwd(), 'src/pages/SessionsPage.module.css'), 'utf8');
    const basePageRule = css.match(/^\.page\s*\{([^}]*)\}/m)?.[1] ?? '';

    expect(basePageRule).toMatch(
      /grid-template-columns:\s*var\(--history-width\)\s+minmax\(0,\s*1fr\)\s+var\(--inspector-width\)/,
    );
    expect(css).toMatch(/@media\s*\(max-width:\s*1179px\)/);
  });

  it('MainPanel opens an in-workbench guide from the help control', () => {
    render(
      <AppStateProvider>
        <ToastProvider>
          <MainPanel connectionState="connected" />
        </ToastProvider>
      </AppStateProvider>,
    );

    fireEvent.click(screen.getByRole('button', { name: 'Open workbench guide' }));

    expect(screen.getByRole('dialog', { name: 'Workbench guide' })).toBeDefined();
    expect(screen.getByText('Start with context')).toBeDefined();
    expect(screen.getByText('Close safely')).toBeDefined();

    fireEvent.click(screen.getByRole('button', { name: 'Close workbench guide' }));
    expect(screen.queryByRole('dialog', { name: 'Workbench guide' })).toBeNull();
  });

  it('AssistantCell copy action uses real SVG icons', () => {
    render(<AssistantCell text="Copy me" />);
    const button = screen.getByRole('button', { name: 'Copy message' });
    expect(button.querySelector('svg')).toBeDefined();
    expect(screen.queryByText('⧉ Copy')).toBeNull();
  });

  it('Sidebar session and world actions use real SVG icons', async () => {
    render(
      <AppStateProvider>
        <SessionIconHarness />
        <WorldIconHarness />
      </AppStateProvider>,
    );

    expect(screen.getByRole('button', { name: 'New session' }).querySelector('svg')).toBeDefined();
    expect(
      screen.getByRole('button', { name: 'Generate title' }).querySelector('svg'),
    ).toBeDefined();
    expect(screen.getByRole('button', { name: '编辑世界' }).querySelector('svg')).toBeDefined();
    expect(screen.queryByText('+')).toBeNull();
  });

  it('SessionList groups live and archived sessions with lifecycle metadata', () => {
    render(
      <AppStateProvider>
        <SessionLifecycleHarness />
      </AppStateProvider>,
    );

    expect(screen.getByText('Active Sessions')).toBeDefined();
    expect(screen.getByText('Archived Sessions')).toBeDefined();
    expect(screen.getByText(/2 turns/)).toBeDefined();
    expect(screen.getByText(/8 turns/)).toBeDefined();
    expect(screen.getByLabelText('Session Planning scene, 2 turns')).toBeDefined();
    expect(screen.getByLabelText('Session Old outline, archived, 8 turns')).toBeDefined();
  });

  it('SessionList selects the session agent and clears a stale agent when absent', () => {
    render(
      <AppStateProvider>
        <ToastProvider>
          <SessionSelectionHarness />
        </ToastProvider>
      </AppStateProvider>,
    );

    fireEvent.click(screen.getByLabelText('Session World notes, 2 turns'));

    expect(screen.getByLabelText('selected-session')).toHaveTextContent('world_session');
    expect(screen.getByLabelText('selected-agent')).toHaveTextContent('none');
  });

  it('UserCell renders text', () => {
    render(<UserCell text="Hello world" />);
    expect(screen.getByText('Hello world')).toBeDefined();
  });

  it('AssistantCell renders markdown', () => {
    render(<AssistantCell text="**bold**" />);
    expect(screen.getByText('bold')).toBeDefined();
  });

  it('AssistantCell renders GFM tables and task lists', () => {
    render(
      <AssistantCell
        text={[
          '| File | Status |',
          '| --- | --- |',
          '| `App.tsx` | Done |',
          '',
          '- [x] Render table',
          '- [ ] Verify mobile wrapping',
        ].join('\n')}
      />,
    );

    expect(screen.getByRole('table')).toBeDefined();
    expect(screen.getByText('App.tsx')).toBeDefined();
    expect(screen.getByText('Render table')).toBeDefined();
    expect(screen.getAllByRole('checkbox')).toHaveLength(2);
  });

  it('SystemCell renders text', () => {
    render(<SystemCell text="Connected" />);
    expect(screen.getByText('Connected')).toBeDefined();
  });

  it('StatusPill renders label', () => {
    render(<StatusPill label="thinking" />);
    expect(screen.getByText('Thinking')).toBeDefined();
  });

  it('ToolCell renders running state', () => {
    render(<ToolCell toolName="read_file" toolRunning={true} />);
    expect(screen.getByText('read_file')).toBeDefined();
    expect(screen.getByText('running...')).toBeDefined();
  });

  it('ToolCell renders done state', () => {
    render(<ToolCell toolName="grep" toolOutput="results" />);
    expect(screen.getByText('grep')).toBeDefined();
    expect(screen.getByText('done')).toBeDefined();
  });
});

describe('Icon source hygiene', () => {
  it('does not use emoji or symbol placeholders for functional icons', () => {
    const files = [
      'src/components/MainPanel.tsx',
      'src/components/cells/AssistantCell.tsx',
      'src/components/Sidebar/SessionList.tsx',
      'src/components/Sidebar/WorldSelector.tsx',
      'src/components/Sidebar.tsx',
      'src/components/Composer.tsx',
    ];
    const source = files.map((file) => readFileSync(join(process.cwd(), file), 'utf8')).join('\n');

    expect(source).not.toMatch(/[☰◫✎⧉✓]/);
    expect(source).not.toContain('鉁?');
    expect(source).not.toContain('脳');
  });
});

describe('Visible copy hygiene', () => {
  it('keeps workbench-facing source free of mojibake copy', () => {
    const source = readFileSync(join(process.cwd(), 'src/components/Composer.tsx'), 'utf8');
    expect(source).not.toMatch(/[璇宸褰€]/);
  });
});

describe('World onboarding', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('keeps first-run labels readable', () => {
    render(
      <AppStateProvider>
        <WorldOnboarding />
      </AppStateProvider>,
    );

    expect(screen.getByRole('heading', { name: /Merak/ })).toBeDefined();
    expect(screen.getByText('Create your first World')).toBeDefined();
    expect(screen.getByRole('button', { name: /创建第一个|Create first/ })).toBeDefined();
    expect(screen.queryByText(/[�鈥鈫鈭]/)).toBeNull();
  });

  it('prioritizes entering an existing world before creation controls', () => {
    function ExistingWorldsHarness() {
      const { dispatch } = useAppState();

      useEffect(() => {
        dispatch({
          type: 'SET_WORLDS',
          worlds: [
            {
              id: 'world_1',
              name: 'Northreach',
              description: 'Snowbound border city',
              created_at: '2026-06-06T10:00:00Z',
              updated_at: '2026-06-07T12:30:00Z',
            },
          ],
        });
      }, [dispatch]);

      return <WorldOnboarding />;
    }

    render(
      <AppStateProvider>
        <ExistingWorldsHarness />
      </AppStateProvider>,
    );

    expect(screen.getByText('Continue a World')).toBeDefined();
    expect(screen.getByText('Northreach')).toBeDefined();
    expect(screen.getByText('Ready to enter')).toBeDefined();
    expect(screen.getByText(/Last opened/)).toBeDefined();
    expect(screen.getByRole('button', { name: 'Enter Northreach' })).toBeDefined();
    expect(screen.getByRole('button', { name: 'Create new world' })).toBeDefined();
    expect(screen.queryByLabelText(/World Name|世界名称/)).toBeNull();
  });

  it('creates a world and optional first character from the first-run flow', async () => {
    const createWorld = vi
      .spyOn(api, 'createWorld')
      .mockResolvedValue({ ok: true, world_id: 'world_99', name: 'Northreach' });
    const createAgent = vi
      .spyOn(api, 'createAgent')
      .mockResolvedValue({ ok: true, agent_id: 'agent_99' });

    render(
      <AppStateProvider>
        <WorldOnboarding />
      </AppStateProvider>,
    );

    fireEvent.change(screen.getByLabelText(/World Name|世界名称/), {
      target: { value: 'Northreach' },
    });
    fireEvent.change(screen.getByLabelText(/Description|一句话/), {
      target: { value: 'Snowbound border city' },
    });
    fireEvent.click(screen.getByRole('button', { name: /创建第一个|Create first/ }));
    fireEvent.change(screen.getByLabelText(/Character Name|人物姓名|角色姓名/), {
      target: { value: 'Lian' },
    });
    fireEvent.change(screen.getByLabelText(/Identity|身份/), {
      target: { value: 'Archivist of the old passes' },
    });
    fireEvent.click(screen.getByRole('button', { name: /Create World|创建世界/ }));

    await screen.findByRole('button', { name: /Creating|创建中/ });
    expect(createWorld).toHaveBeenCalledWith('Northreach', 'Snowbound border city');
    expect(createAgent).toHaveBeenCalledWith('world_99', {
      name: 'Lian',
      identity: 'Archivist of the old passes',
    });
  });

  it('stores the newly created world summary before moving to the dashboard', async () => {
    vi.spyOn(api, 'createWorld').mockResolvedValue({
      ok: true,
      world_id: 'world_99',
      name: 'Northreach',
      description: 'Snowbound border city',
    });

    function CreatedWorldProbe() {
      const { state } = useAppState();
      const created = state.worlds.find((world) => world.id === 'world_99');
      return <output aria-label="created-world">{created?.name ?? 'missing'}</output>;
    }

    render(
      <AppStateProvider>
        <WorldOnboarding />
        <CreatedWorldProbe />
      </AppStateProvider>,
    );

    fireEvent.change(screen.getByLabelText(/World Name|世界名称/), {
      target: { value: 'Northreach' },
    });
    fireEvent.change(screen.getByLabelText(/Description|一句话/), {
      target: { value: 'Snowbound border city' },
    });
    fireEvent.click(screen.getByRole('button', { name: /Create World|创建世界/ }));

    await waitFor(() => {
      expect(screen.getByLabelText('created-world')).toHaveTextContent('Northreach');
    });
  });
});

function WorldDashboardHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({ type: 'SET_WORLD', worldId: 'world_1' });
    dispatch({
      type: 'SET_WORLDBUILDING_DATA',
      worlds: [
        {
          id: 'world_1',
          name: 'Northreach',
          description: 'Snowbound border city',
          created_at: '2026-06-06T10:00:00Z',
        },
      ],
      agents: [],
      foreshadowing: [],
      secrets: [],
      worldTime: 'Day 1, dawn',
      storyOverview: {
        current_arc: {
          id: 'arc_1',
          title: 'The sealed pass',
          status: 'drafting',
        },
        current_chapter: {
          id: 'chapter_1',
          title: 'Arrival at Northreach',
          number: 1,
          status: 'drafting',
          scene_count: 0,
        },
        current_scene: null,
        agents: [],
        foreshadowing: [],
        secrets: [],
        world_time: 'Day 1, dawn',
      },
    });
  }, [dispatch]);

  return <WorldDashboard />;
}

describe('World dashboard', () => {
  it('shows readable navigation and next steps when a world has no agents yet', async () => {
    render(
      <AppStateProvider>
        <WorldDashboardHarness />
      </AppStateProvider>,
    );

    expect(await screen.findByRole('heading', { name: 'Northreach' })).toBeDefined();
    expect(screen.getByRole('button', { name: 'Back to Worlds' })).toBeDefined();
    expect(screen.getByText('Choose an agent lane to start writing')).toBeDefined();
    expect(screen.getByText('Create a character')).toBeDefined();
    expect(screen.getByRole('button', { name: 'Create first character' })).toBeDefined();
    expect(screen.queryByText(/[�鈥鈫鈭]/)).toBeNull();
  });

  it('opens character creation from the empty-world next step', async () => {
    render(
      <AppStateProvider>
        <WorldDashboardHarness />
      </AppStateProvider>,
    );

    fireEvent.click(await screen.findByRole('button', { name: 'Create first character' }));

    expect(screen.getByRole('dialog', { name: 'Create character' })).toBeDefined();
    expect(screen.getByRole('heading', { name: 'Create Character' })).toBeDefined();
  });
});

describe('Desktop boot progress model', () => {
  it('marks runtime startup as active while the desktop runtime is starting', () => {
    const steps = getDesktopRuntimeSteps('starting', {
      phase: 'starting',
      apiBaseUrl: null,
      port: null,
      pid: 101,
      version: '0.1.0',
      pgStatus: 'bundled',
      configPath: 'settings.local.json',
      logPath: 'merak-runtime.log',
      error: null,
    });

    expect(steps.map((step) => `${step.label}:${step.state}`)).toEqual([
      'Local process:active',
      'Local address:waiting',
      'Writing assistant:waiting',
      'Workbench:waiting',
    ]);
  });

  it('surfaces runtime failure in the progress model', () => {
    const steps = getDesktopRuntimeSteps('failed', {
      phase: 'failed',
      apiBaseUrl: null,
      port: null,
      pid: null,
      version: '0.1.0',
      pgStatus: 'external-or-unavailable',
      configPath: 'settings.local.json',
      logPath: 'merak-runtime.log',
      error: 'Runtime exited',
    });

    expect(steps[0]).toMatchObject({
      label: 'Local process',
      state: 'failed',
      detail: 'Runtime exited',
    });
  });
});

describe('ToolPanel source-to-style mapping', () => {
  it('builtin tools map to safe icon and badge', () => {
    const source: string = 'builtin';
    const icon = source === 'mcp' ? 'iconAsk' : 'iconSafe';
    const badge = source === 'mcp' ? 'badgeAsk' : 'badgeSafe';
    expect(icon).toBe('iconSafe');
    expect(badge).toBe('badgeSafe');
  });

  it('mcp tools map to ask icon and badge', () => {
    const source: string = 'mcp';
    const icon = source === 'mcp' ? 'iconAsk' : 'iconSafe';
    const badge = source === 'mcp' ? 'badgeAsk' : 'badgeSafe';
    expect(icon).toBe('iconAsk');
    expect(badge).toBe('badgeAsk');
  });

  it('unknown source falls back to safe', () => {
    const source: string = 'unknown';
    const icon = source === 'mcp' ? 'iconAsk' : 'iconSafe';
    expect(icon).toBe('iconSafe');
  });
});

function InspectorHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({ type: 'SET_WORLD', worldId: 'world_1' });
    dispatch({
      type: 'SET_WORLDBUILDING_DATA',
      worlds: [
        { id: 'world_1', name: 'Northreach', description: 'Snowbound border city', created_at: '' },
      ],
      agents: [{ id: 'a1', name: 'agent_lian', display_name: 'Lian', kind: 'character' }],
      foreshadowing: [{ id: 'f1', content: 'The bell tower never rings at noon', status: 'open' }],
      secrets: [{ id: 's1', title: 'Lian knows the passphrase', status: 'secret' }],
      worldTime: 'Day 4, dusk',
    });
  }, [dispatch]);

  return <InspectorPanel open={true} onClose={() => {}} />;
}

function CreationDashboardHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({ type: 'SET_WORLD', worldId: 'world_1' });
    dispatch({
      type: 'SET_WORLDBUILDING_DATA',
      worlds: [
        { id: 'world_1', name: 'Northreach', description: 'Snowbound border city', created_at: '' },
      ],
      agents: [{ id: 'agent_1', name: 'lian', display_name: 'Lian', kind: 'individual' }],
      foreshadowing: [
        {
          id: 'f1',
          content: 'The old bell never rings at noon.',
          status: 'open',
          pay_off_idea: 'It rings only when the pass opens.',
          tags: ['omen'],
        },
      ],
      secrets: [
        {
          id: 's1',
          title: 'The gate key is fake',
          truth: 'The real key is a song.',
          public_version: 'The brass key opens the north gate.',
          stakes: 'The wrong key traps the caravan.',
          status: 'active',
          aware_character_ids: ['agent_1'],
        },
      ],
      worldTime: 'Day 4, dusk',
    });
    dispatch({ type: 'SET_INSPECTOR_TAB', tab: 'creation' });
  }, [dispatch]);

  return <InspectorPanel open={true} onClose={() => {}} />;
}

function AgentsPanelHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({ type: 'SET_WORLD', worldId: 'world_1' });
    dispatch({
      type: 'SET_WORLDBUILDING_DATA',
      worlds: [
        { id: 'world_1', name: 'Northreach', description: 'Snowbound border city', created_at: '' },
      ],
      agents: [{ id: 'agent_1', name: 'lian', display_name: 'Lian', kind: 'individual' }],
      foreshadowing: [],
      secrets: [],
      worldTime: 'Day 4, dusk',
    });
    dispatch({ type: 'SET_INSPECTOR_TAB', tab: 'agents' });
  }, [dispatch]);

  return <InspectorPanel open={true} onClose={() => {}} />;
}

function FilesHarness({ twoFiles = false }: { twoFiles?: boolean }) {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({
      type: 'SET_OUTPUT_DIRECTORY',
      path: '/Users/me/novel',
    });
    dispatch({
      type: 'REGISTER_GENERATED_FILE',
      file: {
        id: 'file_1',
        title: 'chapter-12',
        path: '/Users/me/novel/chapter-12.md',
        updatedAt: 1,
      },
    });
    dispatch({ type: 'SET_INSPECTOR_TAB', tab: 'files' });
    if (twoFiles) {
      dispatch({
        type: 'REGISTER_GENERATED_FILE',
        file: { id: 'file_2', title: 'notes', path: '/Users/me/novel/notes.txt', updatedAt: 2 },
      });
    }
  }, [dispatch, twoFiles]);

  return <InspectorPanel open={true} onClose={() => {}} />;
}

describe('InspectorPanel', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('renders selected world context and live run state', async () => {
    render(
      <AppStateProvider>
        <InspectorHarness />
      </AppStateProvider>,
    );

    expect(await screen.findByText('Northreach')).toBeDefined();
    expect(screen.getByText('Day 4, dusk')).toBeDefined();
    expect(screen.getByText('Lian')).toBeDefined();
    expect(screen.getByText('The bell tower never rings at noon')).toBeDefined();
    expect(screen.getByText('Lian knows the passphrase')).toBeDefined();
    expect(screen.getByText('世界时间')).toBeDefined();
    expect(screen.getByText('当前：Day 4, dusk')).toBeDefined();
    expect(screen.queryByText(/[�鈥鈫鈭]/)).toBeNull();
  });

  it('renders an empty world state before selection', () => {
    render(
      <AppStateProvider>
        <InspectorPanel open={true} onClose={() => {}} />
      </AppStateProvider>,
    );

    expect(screen.getByText('Select a world to load story context.')).toBeDefined();
  });

  it('lists generated files with external editor entry points', async () => {
    render(
      <AppStateProvider>
        <FilesHarness />
      </AppStateProvider>,
    );

    expect(await screen.findByText('Generated Files')).toBeDefined();
    expect(screen.getByText('/Users/me/novel')).toBeDefined();
    expect(screen.getByText('chapter-12')).toBeDefined();
    expect(screen.getByText('/Users/me/novel/chapter-12.md')).toBeDefined();
    expect(screen.getByRole('button', { name: 'Open chapter-12 in editor' })).toBeDefined();
  });

  it('opens a text editor when a generated file is double-clicked', async () => {
    vi.spyOn(api, 'readWorkspaceFile').mockResolvedValue({
      ok: true,
      file: {
        path: '/Users/me/novel/chapter-12.md',
        content: 'Original draft.',
        encoding: 'utf-8',
        updated_at: '2026-06-16T08:00:00.000Z',
        version: 'v1',
      },
    });

    render(
      <AppStateProvider>
        <FilesHarness />
      </AppStateProvider>,
    );

    fireEvent.doubleClick(await screen.findByText('chapter-12'));
    const editor = await screen.findByLabelText('Edit chapter-12');
    await waitFor(() => expect((editor as HTMLTextAreaElement).value).toBe('Original draft.'));
    fireEvent.change(editor, { target: { value: 'A revised opening line.' } });

    expect((editor as HTMLTextAreaElement).value).toBe('A revised opening line.');
    expect(screen.getAllByText('/Users/me/novel/chapter-12.md')).toHaveLength(2);
  });

  it('marks dirty workspace files and reverts to the loaded editor baseline', async () => {
    vi.spyOn(api, 'readWorkspaceFile').mockResolvedValue({
      ok: true,
      file: {
        path: '/Users/me/novel/chapter-12.md',
        content: 'Original draft.',
        encoding: 'utf-8',
        updated_at: '2026-06-16T08:00:00.000Z',
        version: 'v1',
      },
    });

    render(
      <AppStateProvider>
        <FilesHarness />
      </AppStateProvider>,
    );

    fireEvent.click(await screen.findByRole('button', { name: 'Open chapter-12 in editor' }));
    const editor = await screen.findByLabelText('Edit chapter-12');
    await waitFor(() => expect((editor as HTMLTextAreaElement).value).toBe('Original draft.'));
    fireEvent.change(editor, { target: { value: 'Changed draft.' } });

    expect(screen.getAllByText('Unsaved changes').length).toBeGreaterThan(0);

    fireEvent.click(
      screen.getByRole('button', { name: 'Revert chapter-12 to last loaded version' }),
    );

    expect((editor as HTMLTextAreaElement).value).toBe('Original draft.');
    expect(screen.queryByText('Unsaved changes')).toBeNull();
  });

  it('surfaces save failures in the editor and toast rail', async () => {
    vi.spyOn(api, 'readWorkspaceFile').mockResolvedValue({
      ok: true,
      file: {
        path: '/Users/me/novel/chapter-12.md',
        content: 'Original draft.',
        encoding: 'utf-8',
        updated_at: '2026-06-16T08:00:00.000Z',
        version: 'v1',
      },
    });
    vi.spyOn(api, 'saveWorkspaceFile').mockRejectedValue(new Error('Disk is locked'));

    render(
      <StrictMode>
        <AppStateProvider>
          <ToastProvider>
            <FilesHarness />
          </ToastProvider>
        </AppStateProvider>
      </StrictMode>,
    );

    fireEvent.click(await screen.findByRole('button', { name: 'Open chapter-12 in editor' }));
    const editor = await screen.findByLabelText('Edit chapter-12');
    await waitFor(() => expect((editor as HTMLTextAreaElement).value).toBe('Original draft.'));
    fireEvent.change(editor, { target: { value: 'Changed draft.' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save chapter-12' }));

    expect(await screen.findAllByText('Disk is locked')).toHaveLength(2);
  });

  it('blocks file switching during save and commits the returned version', async () => {
    let resolveSave!: (value: Awaited<ReturnType<typeof api.saveWorkspaceFile>>) => void;
    const pending = new Promise<Awaited<ReturnType<typeof api.saveWorkspaceFile>>>((resolve) => {
      resolveSave = resolve;
    });
    vi.spyOn(api, 'readWorkspaceFile').mockResolvedValue({
      ok: true,
      file: {
        path: '/Users/me/novel/chapter-12.md',
        content: 'Original',
        encoding: 'utf-8',
        updated_at: 'old',
        version: 'v1',
      },
    });
    const save = vi
      .spyOn(api, 'saveWorkspaceFile')
      .mockReturnValueOnce(pending)
      .mockResolvedValueOnce({
        ok: true,
        file: { path: '/Users/me/novel/chapter-12.md', updated_at: 'newer', version: 'v3' },
      });
    render(
      <StrictMode>
        <AppStateProvider>
          <ToastProvider>
            <FilesHarness />
          </ToastProvider>
        </AppStateProvider>
      </StrictMode>,
    );
    fireEvent.click(await screen.findByRole('button', { name: 'Open chapter-12 in editor' }));
    const editor = await screen.findByLabelText('Edit chapter-12');
    fireEvent.change(editor, { target: { value: 'First save' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save chapter-12' }));
    expect(screen.getByRole('button', { name: 'Open chapter-12 in editor' })).toBeDisabled();
    await act(async () =>
      resolveSave({
        ok: true,
        file: { path: '/Users/me/novel/chapter-12.md', updated_at: 'new', version: 'v2' },
      }),
    );
    fireEvent.change(editor, { target: { value: 'Second save' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save chapter-12' }));
    await waitFor(() =>
      expect(save).toHaveBeenLastCalledWith('/Users/me/novel/chapter-12.md', 'Second save', 'v2'),
    );
  });

  it('ignores stale read errors and finally callbacks across A-B-A overlap', async () => {
    let rejectA!: (reason: Error) => void;
    let resolveB!: (value: Awaited<ReturnType<typeof api.readWorkspaceFile>>) => void;
    let resolveNewestA!: (value: Awaited<ReturnType<typeof api.readWorkspaceFile>>) => void;
    const oldA = new Promise<never>((_, reject) => {
      rejectA = reject;
    });
    const oldB = new Promise<Awaited<ReturnType<typeof api.readWorkspaceFile>>>((resolve) => {
      resolveB = resolve;
    });
    const newestA = new Promise<Awaited<ReturnType<typeof api.readWorkspaceFile>>>((resolve) => {
      resolveNewestA = resolve;
    });
    vi.spyOn(api, 'readWorkspaceFile')
      .mockReturnValueOnce(oldA)
      .mockReturnValueOnce(oldB)
      .mockReturnValueOnce(newestA);
    render(
      <AppStateProvider>
        <ToastProvider>
          <FilesHarness twoFiles />
        </ToastProvider>
      </AppStateProvider>,
    );
    const a = await screen.findByRole('button', { name: 'Open chapter-12 in editor' });
    const b = screen.getByRole('button', { name: 'Open notes in editor' });
    fireEvent.click(a);
    fireEvent.click(b);
    fireEvent.click(a);
    await act(async () => rejectA(new Error('Old A failed')));
    await act(async () =>
      resolveB({
        ok: true,
        file: {
          path: '/Users/me/novel/notes.txt',
          content: 'Old B',
          encoding: 'utf-8',
          updated_at: 'old',
          version: 'b1',
        },
      }),
    );
    expect(screen.queryByText('Old A failed')).toBeNull();
    expect(screen.getByRole('button', { name: 'Open chapter-12 in editor' })).toHaveTextContent(
      'Loading...',
    );
    await act(async () =>
      resolveNewestA({
        ok: true,
        file: {
          path: '/Users/me/novel/chapter-12.md',
          content: 'Newest A',
          encoding: 'utf-8',
          updated_at: 'new',
          version: 'a2',
        },
      }),
    );
    expect(await screen.findByLabelText('Edit chapter-12')).toHaveValue('Newest A');
  });

  it('renders a readable creation dashboard without mojibake copy', async () => {
    render(
      <AppStateProvider>
        <CreationDashboardHarness />
      </AppStateProvider>,
    );

    expect(await screen.findByRole('heading', { name: 'Creation Panel' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Create' })).toBeDefined();
    expect(await screen.findByText('Plant and track narrative threads.')).toBeDefined();
    expect(screen.getByText('Open Threads')).toBeDefined();
    expect(screen.getByText('The old bell never rings at noon.')).toBeDefined();
    expect(screen.queryByText(/[�鈥鈫鈭]/)).toBeNull();
  });

  it('shows secrets and relation empty hints from creation tabs', async () => {
    render(
      <AppStateProvider>
        <CreationDashboardHarness />
      </AppStateProvider>,
    );

    fireEvent.click(await screen.findByRole('tab', { name: /Secrets/ }));

    expect(screen.getByText('Manage private truths and knowledge boundaries.')).toBeDefined();
    expect(screen.getByText('Public version: The brass key opens the north gate.')).toBeDefined();
    expect(screen.getByText('Aware: Lian')).toBeDefined();

    fireEvent.click(screen.getByRole('tab', { name: /Relations/ }));

    expect(screen.getByText('Map character ties, alliances, and tension.')).toBeDefined();
    expect(screen.getByText('No explicit relationship edges yet.')).toBeDefined();
  });

  it('opens a readable agent profile and edit form', async () => {
    vi.spyOn(api, 'fetchAgentDetail').mockResolvedValue({
      ok: true,
      agent: {
        id: 'agent_1',
        world_id: 'world_1',
        name: 'lian',
        display_name: 'Lian',
        kind: 'individual',
        created_at: '2026-06-16T08:00:00Z',
        updated_at: '2026-06-16T08:00:00Z',
        images: { avatar: [], design: [] },
        character_card: {
          version: 1,
          age: 28,
          gender: 'Female',
          race: 'Human',
          identity: 'Archivist',
          core_traits: ['patient', 'watchful'],
          speaking_style: 'Measured',
          core_desire: 'Protect the archive',
          deep_fear: 'Forgetting the old treaties',
        },
      },
    });

    render(
      <AppStateProvider>
        <AgentsPanelHarness />
      </AppStateProvider>,
    );

    const agentName = await screen.findByText('Lian');
    const agentButton = agentName.closest('[role="button"]');
    expect(agentButton).toBeDefined();
    fireEvent.click(agentButton!);

    expect(await screen.findByRole('heading', { name: 'Lian' })).toBeDefined();
    expect(screen.getByText('Basics')).toBeDefined();
    expect(screen.getByText('Core Traits')).toBeDefined();
    expect(screen.getByText('Speaking Style')).toBeDefined();
    expect(screen.queryByText(/[�鈥鈫鈭]/)).toBeNull();

    fireEvent.click(screen.getByRole('button', { name: 'Edit' }));

    expect(screen.getByRole('heading', { name: 'Edit Lian' })).toBeDefined();
    expect(screen.getByText('Core traits (comma or space separated)')).toBeDefined();
    expect(screen.getByRole('button', { name: 'Save' })).toBeDefined();
  });
});
