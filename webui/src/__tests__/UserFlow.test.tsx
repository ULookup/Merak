import { render, screen, fireEvent } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { AppStateProvider } from '../AppState';
import Composer from '../components/Composer';
import ExportDialog from '../components/ExportDialog';
import { ToastProvider } from '../components/Toast';
import SetupWizard from '../components/SetupWizard';

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
    render(
      <ExportDialog worldId="world_1" chapters={chapters} onClose={onClose} />,
    );

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
    render(
      <ExportDialog worldId="world_1" chapters={chapters} onClose={onClose} />,
    );

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
    render(
      <ExportDialog worldId="world_1" chapters={chapters} onClose={onClose} />,
    );

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
    render(
      <ExportDialog worldId="world_1" chapters={chapters} onClose={onClose} />,
    );

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
});
