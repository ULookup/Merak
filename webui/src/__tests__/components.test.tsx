import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { useEffect, useState } from 'react';
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { AppStateProvider, useAppState } from '../AppState';
import BrandMark from '../components/BrandMark';
import AssistantCell from '../components/cells/AssistantCell';
import StatusPill from '../components/cells/StatusPill';
import SystemCell from '../components/cells/SystemCell';
import ToolCell from '../components/cells/ToolCell';
import UserCell from '../components/cells/UserCell';
import ChatTimeline from '../components/ChatTimeline';
import HelpDrawer from '../components/HelpDrawer';
import InspectorPanel from '../components/InspectorPanel';
import MainPanel from '../components/MainPanel';
import SessionList from '../components/Sidebar/SessionList';
import WorldSelector from '../components/Sidebar/WorldSelector';
import { ToastProvider } from '../components/Toast';

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

function HelpDrawerHarness() {
  const [open, setOpen] = useState(false);

  return (
    <AppStateProvider>
      <ToastProvider>
        <MainPanel onOpenGuide={() => setOpen(true)} connectionState="connected" />
        <HelpDrawer open={open} onClose={() => setOpen(false)} />
      </ToastProvider>
    </AppStateProvider>
  );
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
    expect(screen.getByText('SSE connected')).toBeDefined();
    expect(screen.getByText('claude-sonnet-4-6')).toBeDefined();
    expect(screen.getByText('1.3K tokens')).toBeDefined();
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
          <MainPanel connectionState="connected" />
        </ToastProvider>
      </AppStateProvider>,
    );

    expect(screen.getByRole('button', { name: 'Open sidebar' }).querySelector('svg')).toBeDefined();
    expect(
      screen.getByRole('button', { name: 'Open inspector' }).querySelector('svg'),
    ).toBeDefined();
  });

  it('AssistantCell copy action uses real SVG icons', () => {
    render(<AssistantCell text="Copy me" />);
    const button = screen.getByRole('button', { name: 'Copy message' });
    expect(button.querySelector('svg')).toBeDefined();
  });

  it('opens and closes the workbench guide from the help button', async () => {
    render(<HelpDrawerHarness />);

    fireEvent.click(screen.getByRole('button', { name: 'Open workbench guide' }));

    expect(await screen.findByRole('dialog', { name: 'Merak guide' })).toBeDefined();
    expect(screen.getByText('Create a world')).toBeDefined();
    expect(screen.getByText('Run and stream')).toBeDefined();

    fireEvent.click(screen.getByRole('button', { name: 'Close guide' }));

    expect(screen.queryByRole('dialog', { name: 'Merak guide' })).toBeNull();
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
    expect(screen.getByRole('button', { name: 'Edit world' }).querySelector('svg')).toBeDefined();
    expect(screen.queryByText('+')).toBeNull();
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
  it('does not leave mojibake in primary workbench components', () => {
    const files = [
      'src/components/MainPanel.tsx',
      'src/components/Inspector/StoryInspector.tsx',
      'src/components/Sidebar/PipelineNavigator.tsx',
      'src/components/Sidebar/WorkflowMonitor.tsx',
      'src/components/cells/AssistantCell.tsx',
      'src/components/Sidebar/SessionList.tsx',
      'src/components/Sidebar/WorldSelector.tsx',
      'src/components/Sidebar.tsx',
    ];
    const source = files.map((file) => readFileSync(join(process.cwd(), file), 'utf8')).join('');

    expect(source).not.toMatch(/[涓褰宸鈥鉁瑙鍦鍒鑷鎵锛路]/);
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

function FilesHarness() {
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
  }, [dispatch]);

  return <InspectorPanel open={true} onClose={() => {}} />;
}

describe('InspectorPanel', () => {
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
    render(
      <AppStateProvider>
        <FilesHarness />
      </AppStateProvider>,
    );

    fireEvent.doubleClick(await screen.findByText('chapter-12'));
    const editor = await screen.findByLabelText('Edit chapter-12');
    fireEvent.change(editor, { target: { value: 'A revised opening line.' } });

    expect((editor as HTMLTextAreaElement).value).toBe('A revised opening line.');
    expect(screen.getAllByText('/Users/me/novel/chapter-12.md')).toHaveLength(2);
  });
});
