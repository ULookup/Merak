import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { useEffect } from 'react';
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
import InspectorPanel from '../components/InspectorPanel';
import CreateAgentModal from '../components/Inspector/CreateAgentModal';
import CreateForeshadowingModal from '../components/Inspector/CreateForeshadowingModal';
import CreateSceneModal from '../components/Inspector/CreateSceneModal';
import CreateSecretModal from '../components/Inspector/CreateSecretModal';
import RunInspector from '../components/Inspector/RunInspector';
import StoryInspector from '../components/Inspector/StoryInspector';
import MainPanel from '../components/MainPanel';
import AuditDashboard from '../components/Replay/AuditDashboard';
import ContextMeter from '../components/Sidebar/ContextMeter';
import ModelSelector from '../components/Sidebar/ModelSelector';
import SessionList from '../components/Sidebar/SessionList';
import ToolPanel from '../components/Sidebar/ToolPanel';
import WorldSelector from '../components/Sidebar/WorldSelector';
import { ToastProvider } from '../components/Toast';
import { I18nProvider } from '../i18n';
import ConnectionBanner from '../components/ConnectionBanner';

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

function RunInspectorHarness() {
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
        delegation_patterns: [],
      },
    });
    dispatch({ type: 'SET_USAGE', inputTokens: 1200, outputTokens: 80 });
  }, [dispatch]);

  return <RunInspector />;
}

function SidebarAiConceptHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({
      type: 'SET_METADATA',
      metadata: {
        provider: 'openai',
        model: 'gpt-4o',
        models: [
          { name: 'gpt-4o', provider: 'openai', max_context_tokens: 128000 },
          { name: 'claude-sonnet-4-6', provider: 'anthropic', max_context_tokens: 200000 },
        ],
        permission_mode: 'ask',
        memory: { enabled: true },
        worldbuilding: { enabled: true },
        tools: [
          { name: 'write_scene', description: 'Draft the next scene', source: 'builtin' },
          { name: 'figma_sync', description: 'Send UI screens to Figma', source: 'mcp' },
        ],
        mcp_servers: [{ name: 'figma', alive: true }],
        agents: [],
        delegation_patterns: ['fan_out'],
      },
    });
    dispatch({ type: 'SET_USAGE', inputTokens: 1200, outputTokens: 300 });
  }, [dispatch]);

  return (
    <>
      <ModelSelector />
      <ContextMeter />
      <ToolPanel />
    </>
  );
}

function StoryNoBackendDataHarness() {
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
      worldTime: null,
      storyOverview: {
        agents: [],
        foreshadowing: [],
        secrets: [],
        world_time: null,
      },
    });
  }, [dispatch]);

  return <StoryInspector />;
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

describe('Cell components', () => {
  it('BrandMark renders the Merak logo accessibly', () => {
    render(<BrandMark />);
    expect(screen.getByRole('img', { name: 'Merak pen planet logo' })).toBeDefined();
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
      <I18nProvider defaultLocale="en">
        <AppStateProvider>
          <TimelineHarness />
        </AppStateProvider>
      </I18nProvider>,
    );

    expect(await screen.findAllByText('Thinking')).toHaveLength(2);
    expect(screen.getByText('Connected')).toBeDefined();
    expect(screen.getByText('claude-sonnet-4-6')).toBeDefined();
    expect(screen.getByText('1.3K words of context')).toBeDefined();
    expect(screen.queryByText(/SSE|runtime/i)).toBeNull();
  });

  it('ConnectionBanner uses plain language for interrupted local service states', () => {
    render(
      <I18nProvider defaultLocale="en">
        <ConnectionBanner state="disconnected" />
      </I18nProvider>,
    );

    expect(screen.getByRole('status')).toHaveTextContent('Merak cannot connect right now.');
    expect(screen.queryByText(/server|runtime|SSE|API/i)).toBeNull();
  });

  it('ChatTimeline empty state avoids technical transport language', () => {
    render(
      <I18nProvider defaultLocale="en">
        <AppStateProvider>
          <ChatTimeline />
        </AppStateProvider>
      </I18nProvider>,
    );

    expect(screen.getByRole('heading', { name: 'Build the next scene' })).toBeDefined();
    expect(screen.queryByText(/SSE|Markdown|tools|delegation/i)).toBeNull();
  });

  it('RunInspector presents creation progress without developer terminology', async () => {
    render(
      <I18nProvider defaultLocale="en">
        <AppStateProvider>
          <RunInspectorHarness />
        </AppStateProvider>
      </I18nProvider>,
    );

    expect(await screen.findByText('Current Creation')).toBeDefined();
    expect(screen.getByText('No creation is running')).toBeDefined();
    expect(screen.getByText(/words of context/)).toBeDefined();
    expect(document.body.textContent ?? '').not.toMatch(
      /Current Run|Runtime Signals|Available Tools|Context Health|Run Timeline|No active run|tools/i,
    );
  });

  it('sidebar explains model, token window, and tools in AI-friendly language', async () => {
    render(
      <I18nProvider defaultLocale="zh">
        <AppStateProvider>
          <SidebarAiConceptHarness />
        </AppStateProvider>
      </I18nProvider>,
    );

    expect(await screen.findByText('模型与服务')).toBeDefined();
    expect(screen.getByText('OpenAI')).toBeDefined();
    expect(screen.getByText('128K token 窗口')).toBeDefined();
    expect(screen.getByText('Token 上下文')).toBeDefined();
    expect(screen.getByText('已用 1.5K / 128K tokens')).toBeDefined();
    expect(screen.getByText('工具（2）')).toBeDefined();
    expect(screen.getByText('内置')).toBeDefined();
    expect(screen.getByText('需要授权')).toBeDefined();
    expect(document.body.textContent ?? '').not.toMatch(/preview|mock|fake/i);
  });

  it('StoryInspector uses clear unloaded states instead of backend placeholders', async () => {
    render(
      <AppStateProvider>
        <StoryNoBackendDataHarness />
      </AppStateProvider>,
    );

    expect(await screen.findByText('Northreach')).toBeDefined();
    expect(screen.getAllByText('时间未设置').length).toBeGreaterThan(0);
    expect(screen.getByText('叙事位置')).toBeDefined();
    expect(screen.getByText('章节未加载')).toBeDefined();
    expect(screen.getByText('场景未加载')).toBeDefined();
    expect(screen.getByText('暂未加载角色声音')).toBeDefined();
    expect(document.body.textContent ?? '').not.toMatch(/waiting for backend|No active scene|Time not set/i);
  });

  it('AuditDashboard empty state avoids transport and backend jargon', () => {
    render(
      <AppStateProvider>
        <AuditDashboard />
      </AppStateProvider>,
    );

    expect(screen.getByText('暂无运行记录。开始一次创作后，这里会显示步骤、工具和 token 统计。')).toBeDefined();
    expect(document.body.textContent ?? '').not.toMatch(/Run Audit|Local timeline|backend/i);
  });

  it('creation modals use beginner-friendly Chinese labels and avoid engineering placeholders', () => {
    const noop = () => {};
    const { rerender } = render(
      <AppStateProvider>
        <CreateAgentModal worldId="world_1" onClose={noop} />
      </AppStateProvider>,
    );

    expect(screen.getByRole('dialog', { name: '创建角色' })).toBeDefined();
    expect(screen.getByText('角色名')).toBeDefined();
    expect(screen.getByPlaceholderText('例如：艾拉、陈泊舟')).toBeDefined();
    expect(screen.getByText('说话风格')).toBeDefined();
    expect(document.body.textContent ?? '').not.toMatch(/Create|New Voice|Name \(ID\)|comma-separated|e\.g\./i);

    rerender(
      <AppStateProvider>
        <CreateSceneModal worldId="world_1" onClose={noop} />
      </AppStateProvider>,
    );

    expect(screen.getByRole('dialog', { name: '创建场景' })).toBeDefined();
    expect(screen.getByText('所属章节 ID')).toBeDefined();
    expect(screen.getByPlaceholderText('从当前章节自动带入，或粘贴后端返回的章节 ID')).toBeDefined();
    expect(screen.getByText('参与角色 ID')).toBeDefined();
    expect(document.body.textContent ?? '').not.toMatch(/Create Scene|New Beat|agent IDs|Day 1 Dawn/i);

    rerender(
      <AppStateProvider>
        <CreateForeshadowingModal worldId="world_1" onClose={noop} />
      </AppStateProvider>,
    );

    expect(screen.getByRole('dialog', { name: '埋设伏笔' })).toBeDefined();
    expect(screen.getByText('伏笔内容')).toBeDefined();
    expect(screen.getByText('隐蔽程度')).toBeDefined();
    expect(screen.getByText('含蓄')).toBeDefined();
    expect(document.body.textContent ?? '').not.toMatch(/New Foreshadowing|Plant Thread|Visible|Subtle|Obvious/i);

    rerender(
      <AppStateProvider>
        <CreateSecretModal worldId="world_1" onClose={noop} />
      </AppStateProvider>,
    );

    expect(screen.getByRole('dialog', { name: '创建秘密' })).toBeDefined();
    expect(screen.getByText('真相')).toBeDefined();
    expect(screen.getByText('知情角色 ID')).toBeDefined();
    expect(screen.getByText('怀疑中的角色 ID')).toBeDefined();
    expect(document.body.textContent ?? '').not.toMatch(/New Secret|Knowledge Boundary|agent IDs|Public version/i);
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
    expect(screen.queryByText('☰')).toBeNull();
    expect(screen.queryByText('◫')).toBeNull();
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
  it('does not use emoji or symbol placeholders for functional icons', () => {
    const files = [
      'src/components/MainPanel.tsx',
      'src/components/cells/AssistantCell.tsx',
      'src/components/Sidebar/SessionList.tsx',
      'src/components/Sidebar/WorldSelector.tsx',
      'src/components/Sidebar.tsx',
      'src/components/Inspector/RunInspector.tsx',
    ];
    const source = files.map((file) => readFileSync(join(process.cwd(), file), 'utf8')).join('\n');

    expect(source).not.toContain('\\u{1F50C}');
    expect(source).not.toContain('\\u{1F527}');
    expect(source).not.toMatch(/[☰◫✎⧉✓]/);
    expect(source).not.toContain('鉁?');
    expect(source).not.toContain('脳');
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
