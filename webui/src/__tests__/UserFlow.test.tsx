import { useEffect } from 'react';
import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { api } from '../api/client';
import { AppStateProvider, useAppState } from '../AppState';
import Composer from '../components/Composer';
import ExportDialog from '../components/ExportDialog';
import SetupWizard from '../components/SetupWizard';
import { ToastProvider } from '../components/Toast';
import ScenesPage from '../pages/ScenesPage';
import SessionsPage from '../pages/SessionsPage';

function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((resolvePromise) => {
    resolve = resolvePromise;
  });
  return { promise, resolve };
}

function SessionsPageHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({ type: 'SET_WORLD', worldId: 'world_1' });
    dispatch({ type: 'SET_SESSION', sessionId: 'session_1' });
    dispatch({ type: 'SET_CURRENT_RUN', runId: 'run_1' });
    dispatch({ type: 'SET_STATUS', status: 'thinking' });
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
      worldTime: 'Day 4, dusk',
    });
    dispatch({
      type: 'SET_SESSIONS',
      sessions: [
        {
          id: 'session_1',
          title: 'Plan the rain invasion',
          world_id: 'world_1',
          agent_id: null,
          last_seq: 6,
          created_at: '2026-06-06T10:30:00Z',
          updated_at: '2026-06-06T10:35:00Z',
          archived_at: null,
        },
      ],
    });
    dispatch({ type: 'SET_INSPECTOR_TAB', tab: 'run' });
  }, [dispatch]);

  return <SessionsPage connectionState="connected" />;
}

describe('Sessions workbench', () => {
  it('keeps session history, conversation, context, composer, and execution state together', async () => {
    Object.defineProperty(window, 'innerWidth', { configurable: true, value: 1440 });
    render(
      <AppStateProvider>
        <ToastProvider>
          <SessionsPageHarness />
        </ToastProvider>
      </AppStateProvider>,
    );

    expect(await screen.findByRole('region', { name: 'Session history' })).toBeDefined();
    expect(screen.getByRole('main', { name: 'Conversation' })).toBeDefined();
    expect(screen.getByRole('complementary', { name: 'Story inspector' })).toBeDefined();
    expect(screen.getByRole('heading', { name: 'Plan the rain invasion' })).toBeDefined();
    expect(screen.getByTestId('composer-input')).toBeDefined();
    for (const tab of ['Story', 'Files', 'Agents', 'Create', 'Run']) {
      expect(screen.getByRole('tab', { name: tab })).toBeDefined();
    }
    expect(screen.getAllByText('Thinking').length).toBeGreaterThan(0);
    expect(await screen.findByText('Creation in progress')).toBeDefined();
  });

  it('hides and restores history and inspector panels with truthful toggle state', async () => {
    Object.defineProperty(window, 'innerWidth', { configurable: true, value: 1440 });
    render(
      <AppStateProvider>
        <ToastProvider>
          <SessionsPageHarness />
        </ToastProvider>
      </AppStateProvider>,
    );

    const history = await screen.findByRole('region', { name: 'Session history' });
    const inspector = screen.getByRole('complementary', { name: 'Story inspector' });
    expect(history).toHaveAttribute('aria-hidden', 'false');
    expect(inspector).toHaveAttribute('aria-hidden', 'false');

    fireEvent.click(screen.getByTestId('menu-btn'));
    expect(history).toHaveAttribute('aria-hidden', 'true');
    expect(history.className).not.toContain('historyOpen');
    expect(screen.getByRole('button', { name: 'Open session history' })).toHaveAttribute(
      'aria-expanded',
      'false',
    );

    fireEvent.click(screen.getByTestId('inspector-btn'));
    expect(inspector).toHaveAttribute('aria-hidden', 'true');
    expect(inspector.className).not.toContain('panelOpen');
    expect(screen.getByRole('button', { name: 'Open inspector' })).toHaveAttribute(
      'aria-expanded',
      'false',
    );
  });

  it('opens the inspector from its tablet default without exposing a hidden-state mismatch', async () => {
    Object.defineProperty(window, 'innerWidth', { configurable: true, value: 1024 });
    render(
      <AppStateProvider>
        <ToastProvider>
          <SessionsPageHarness />
        </ToastProvider>
      </AppStateProvider>,
    );

    const inspector = await screen.findByTestId('inspector-panel');
    expect(inspector).toHaveAttribute('aria-hidden', 'true');
    fireEvent.click(screen.getByRole('button', { name: 'Open inspector' }));
    expect(inspector).toHaveAttribute('aria-hidden', 'false');
    expect(inspector.className).toContain('panelOpen');
  });
});

describe('Scene completion flow', () => {
  it('marks a scene complete only after endScene resolves and renders returned extraction counts', async () => {
    const ending = deferred<Awaited<ReturnType<typeof api.endScene>>>();
    vi.spyOn(api, 'listScenes').mockResolvedValue({
      ok: true,
      scenes: [
        {
          id: 'scene-1',
          title: 'Crossing the flooded stacks',
          chapter_id: 'chapter-2',
          world_time: 'Day 4, dusk',
          status: 'writing',
          participant_ids: ['lin'],
          updated_at: '2026-06-19T09:00:00Z',
        },
      ],
    });
    vi.spyOn(api, 'endScene').mockReturnValue(ending.promise);

    render(
      <AppStateProvider>
        <ScenesPage worldId="world-1" />
      </AppStateProvider>,
    );
    fireEvent.click(await screen.findByRole('option', { name: /Crossing the flooded stacks/ }));
    expect(screen.getByText('writing')).toBeDefined();
    fireEvent.click(screen.getByRole('button', { name: 'End scene' }));
    const dialog = screen.getByRole('dialog', { name: 'End scene' });
    fireEvent.click(within(dialog).getByRole('button', { name: 'End Scene' }));

    expect(screen.getByText('writing')).toBeDefined();
    expect(screen.queryByText('completed')).toBeNull();

    await act(async () =>
      ending.resolve({
        ok: true,
        diaries_written: [{ id: 'diary-1', agent_id: 'lin', scene_id: 'scene-1' }],
        diary_count: 1,
        relations_updated: 2,
        proposed_foreshadowing: [{ id: 'thread-1', content: 'The archive glass remembers.' }],
        leak_risks: 0,
      }),
    );

    await waitFor(() => expect(screen.getByText('completed')).toBeDefined());
    expect(screen.getByText('diaries written').parentElement).toHaveTextContent('1');
    expect(screen.getByText('relations updated').parentElement).toHaveTextContent('2');
    expect(screen.getByText('The archive glass remembers.')).toBeDefined();
  });
});

describe('SetupWizard', () => {
  it('renders the provider selection step initially', () => {
    const onComplete = vi.fn();
    render(<SetupWizard onComplete={onComplete} />);

    // Step title is visible
    expect(screen.getByText('初始化设置')).toBeDefined();

    // Provider dropdown with all options
    const select = screen.getByRole('combobox');
    expect(select).toBeDefined();
    expect(screen.getByText('LLM 提供商')).toBeDefined();
    expect(screen.getByText('OpenAI')).toBeDefined();
    expect(screen.getByText('Anthropic')).toBeDefined();
    expect(screen.getByText('DeepSeek')).toBeDefined();
    expect(screen.getByText('自定义')).toBeDefined();

    // Next button
    expect(screen.getByRole('button', { name: '下一步' })).toBeDefined();
  });

  it('advances to the key step when Next is clicked', () => {
    const onComplete = vi.fn();
    render(<SetupWizard onComplete={onComplete} />);

    fireEvent.click(screen.getByRole('button', { name: '下一步' }));

    // Now on the key step
    expect(screen.getByPlaceholderText('请输入您的 API 密钥')).toBeDefined();
    expect(screen.getByPlaceholderText(/gpt-4o/)).toBeDefined();
    expect(screen.getByRole('button', { name: '保存并测试' })).toBeDefined();
    expect(screen.getByRole('button', { name: '返回' })).toBeDefined();
  });

  it('shows validation error when provider is empty on next step', () => {
    const onComplete = vi.fn();
    render(<SetupWizard onComplete={onComplete} />);

    // By default provider is 'openai', so next step works
    fireEvent.click(screen.getByRole('button', { name: '下一步' }));
    expect(screen.getByPlaceholderText('请输入您的 API 密钥')).toBeDefined();
    expect(screen.getByRole('button', { name: '保存并测试' })).toBeDefined();
  });

  it('renders all provider options in the dropdown', () => {
    const onComplete = vi.fn();
    render(<SetupWizard onComplete={onComplete} />);

    const options = screen.getAllByRole('option');
    expect(options).toHaveLength(4);
    expect(options.map((o) => o.textContent)).toEqual([
      'OpenAI',
      'Anthropic',
      'DeepSeek',
      '自定义',
    ]);
  });
});

describe('ExportDialog', () => {
  const chapters = [
    { id: 'ch1', title: 'The Beginning', number: 1 },
    { id: 'ch2', title: 'The Journey', number: 2 },
    { id: 'ch3', title: 'The End', number: 3 },
  ];

  it('renders the export dialog with chapter checkboxes', () => {
    const onClose = vi.fn();
    render(<ExportDialog worldId="world_1" chapters={chapters} onClose={onClose} />);

    // Dialog title
    expect(screen.getByText('导出 TXT')).toBeDefined();

    // Chapter checkboxes
    expect(screen.getByLabelText(/第1章 The Beginning/)).toBeDefined();
    expect(screen.getByLabelText(/第2章 The Journey/)).toBeDefined();
    expect(screen.getByLabelText(/第3章 The End/)).toBeDefined();

    // Select all / deselect all buttons
    expect(screen.getByRole('button', { name: '全选' })).toBeDefined();
    expect(screen.getByRole('button', { name: '取消全选' })).toBeDefined();

    // Title and author fields
    expect(screen.getByPlaceholderText('请输入书名')).toBeDefined();
    expect(screen.getByPlaceholderText('请输入作者名')).toBeDefined();

    // Action buttons
    expect(screen.getByRole('button', { name: '取消' })).toBeDefined();
    expect(screen.getByRole('button', { name: '导出' })).toBeDefined();
  });

  it('all checking and unchecking of chapters via select-all and deselect-all', () => {
    const onClose = vi.fn();
    render(<ExportDialog worldId="world_1" chapters={chapters} onClose={onClose} />);

    // All checkboxes should be checked by default
    const checkbox1 = screen.getByLabelText(/第1章 The Beginning/) as HTMLInputElement;
    const checkbox2 = screen.getByLabelText(/第2章 The Journey/) as HTMLInputElement;
    const checkbox3 = screen.getByLabelText(/第3章 The End/) as HTMLInputElement;
    expect(checkbox1.checked).toBe(true);
    expect(checkbox2.checked).toBe(true);
    expect(checkbox3.checked).toBe(true);

    // Deselect all
    fireEvent.click(screen.getByRole('button', { name: '取消全选' }));
    expect(checkbox1.checked).toBe(false);
    expect(checkbox2.checked).toBe(false);
    expect(checkbox3.checked).toBe(false);

    // Select all
    fireEvent.click(screen.getByRole('button', { name: '全选' }));
    expect(checkbox1.checked).toBe(true);
    expect(checkbox2.checked).toBe(true);
    expect(checkbox3.checked).toBe(true);
  });

  it('export button is disabled until title is entered and at least one chapter is selected', () => {
    const onClose = vi.fn();
    render(<ExportDialog worldId="world_1" chapters={chapters} onClose={onClose} />);

    const exportBtn = screen.getByRole('button', { name: '导出' });

    // Chapters are all selected, but title is empty — export should be disabled
    expect((exportBtn as HTMLButtonElement).disabled).toBe(true);

    // Deselect all chapters — still disabled
    fireEvent.click(screen.getByRole('button', { name: '取消全选' }));
    expect((exportBtn as HTMLButtonElement).disabled).toBe(true);

    // Select a chapter and enter a title
    const checkbox1 = screen.getByLabelText(/第1章 The Beginning/);
    fireEvent.click(checkbox1);
    const titleInput = screen.getByPlaceholderText('请输入书名');
    fireEvent.change(titleInput, { target: { value: 'My Novel' } });

    expect((exportBtn as HTMLButtonElement).disabled).toBe(false);
  });

  it('calls onClose when cancel button is clicked', () => {
    const onClose = vi.fn();
    render(<ExportDialog worldId="world_1" chapters={chapters} onClose={onClose} />);

    fireEvent.click(screen.getByRole('button', { name: '取消' }));
    expect(onClose).toHaveBeenCalledTimes(1);
  });
});

describe('Composer', () => {
  it('renders feedback buttons', () => {
    // Composer uses useAppState which requires AppStateProvider
    render(
      <AppStateProvider>
        <ToastProvider>
          <Composer />
        </ToastProvider>
      </AppStateProvider>,
    );

    // Feedback buttons
    expect(screen.getByRole('button', { name: '继续写' })).toBeDefined();
    expect(screen.getByRole('button', { name: '改一下' })).toBeDefined();
    expect(screen.getByRole('button', { name: '说说想法' })).toBeDefined();
  });

  it('renders creative prompt mode buttons', () => {
    render(
      <AppStateProvider>
        <ToastProvider>
          <Composer />
        </ToastProvider>
      </AppStateProvider>,
    );

    // Prompt mode buttons (from the mode rail) — accessible name is the text content
    expect(screen.getByRole('button', { name: /^Scene$/ })).toBeDefined();
    expect(screen.getByRole('button', { name: /^Character$/ })).toBeDefined();
    expect(screen.getByRole('button', { name: /^World Rule$/ })).toBeDefined();
    expect(screen.getByRole('button', { name: /^Outline$/ })).toBeDefined();
    expect(screen.getByRole('button', { name: /^Rewrite$/ })).toBeDefined();
  });

  it('renders the textarea input', () => {
    render(
      <AppStateProvider>
        <ToastProvider>
          <Composer />
        </ToastProvider>
      </AppStateProvider>,
    );

    const textarea = screen.getByTestId('composer-input');
    expect(textarea).toBeDefined();
    expect((textarea as HTMLTextAreaElement).disabled).toBe(false);
  });

  it('renders the send button', () => {
    render(
      <AppStateProvider>
        <ToastProvider>
          <Composer />
        </ToastProvider>
      </AppStateProvider>,
    );

    const sendBtn = screen.getByTestId('send-btn');
    expect(sendBtn).toBeDefined();
    // Send button is disabled when text is empty
    expect((sendBtn as HTMLButtonElement).disabled).toBe(true);
  });

  it('cannot start a run without a selected session', () => {
    const startRun = vi.spyOn(api, 'startRun');
    render(
      <AppStateProvider>
        <ToastProvider>
          <Composer />
        </ToastProvider>
      </AppStateProvider>,
    );

    fireEvent.change(screen.getByTestId('composer-input'), { target: { value: 'Draft a scene' } });
    expect(screen.getByTestId('send-btn')).toBeDisabled();
    fireEvent.click(screen.getByTestId('send-btn'));
    expect(startRun).not.toHaveBeenCalled();
  });
});
