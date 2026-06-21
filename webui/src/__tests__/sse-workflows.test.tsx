import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { runtimeApi } from '../api/runtime';
import AskUserPrompt from '../components/AskUserPrompt';
import CreationRequestDialog from '../components/CreationRequestDialog';

const apiMocks = vi.hoisted(() => ({
  respondToAsk: vi.fn(),
  resolveCreation: vi.fn(),
}));

vi.mock('../api/client', () => ({
  api: apiMocks,
  formatApiError: (error: unknown, fallback = 'Request failed') =>
    error instanceof Error ? error.message : fallback,
}));

describe('interactive SSE workflows', () => {
  beforeEach(() => vi.clearAllMocks());

  it('exposes the ask-user response endpoint', () => {
    expect(runtimeApi.respondToAsk).toBeTypeOf('function');
  });

  it('submits an ask response once and resolves only after success', async () => {
    let complete!: () => void;
    apiMocks.respondToAsk.mockReturnValue(
      new Promise<void>((resolve) => {
        complete = resolve;
      }),
    );
    const onResolved = vi.fn();
    render(
      <AskUserPrompt
        request={{
          runId: 'run_1',
          callId: 'call_1',
          question: 'Choose POV',
          choices: ['First person'],
        }}
        onResolved={onResolved}
      />,
    );

    fireEvent.click(screen.getByRole('button', { name: 'First person' }));
    const submit = screen.getByRole('button', { name: 'Send response' });
    fireEvent.click(submit);
    fireEvent.click(submit);

    expect(apiMocks.respondToAsk).toHaveBeenCalledOnce();
    expect(apiMocks.respondToAsk).toHaveBeenCalledWith('run_1', 'call_1', 'First person');
    expect(onResolved).not.toHaveBeenCalled();
    expect(screen.getByRole('button', { name: 'Sending...' })).toBeDisabled();

    complete();
    await waitFor(() => expect(onResolved).toHaveBeenCalledOnce());
    expect(onResolved).toHaveBeenCalledWith('call_1');
  });

  it('keeps the ask dialog open and formats request errors', async () => {
    apiMocks.respondToAsk.mockRejectedValue(new Error('Run is no longer waiting'));
    const onResolved = vi.fn();
    render(
      <AskUserPrompt
        request={{ runId: 'run_1', callId: 'call_1', question: 'Continue?', multiSelect: false }}
        onResolved={onResolved}
      />,
    );
    fireEvent.change(screen.getByLabelText('Your response'), { target: { value: 'Yes' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send response' }));

    expect(await screen.findByRole('alert')).toHaveTextContent('Run is no longer waiting');
    expect(onResolved).not.toHaveBeenCalled();
  });

  it('toggles multiple ask options and exposes their selected state', async () => {
    apiMocks.respondToAsk.mockResolvedValue({ ok: true });
    render(
      <AskUserPrompt
        request={{
          runId: 'run_1',
          callId: 'call_1',
          question: 'Choose themes',
          choices: ['Hope', 'Loss', 'Memory'],
          multiSelect: true,
        }}
        onResolved={vi.fn()}
      />,
    );
    const hope = screen.getByRole('button', { name: 'Hope' });
    const loss = screen.getByRole('button', { name: 'Loss' });
    fireEvent.click(hope);
    fireEvent.click(loss);
    expect(hope).toHaveAttribute('aria-pressed', 'true');
    expect(loss).toHaveAttribute('aria-pressed', 'true');
    fireEvent.click(hope);
    expect(hope).toHaveAttribute('aria-pressed', 'false');
    fireEvent.click(screen.getByRole('button', { name: 'Send response' }));
    await waitFor(() =>
      expect(apiMocks.respondToAsk).toHaveBeenCalledWith('run_1', 'call_1', 'Loss'),
    );
  });

  it('resets ask input and errors when request identity changes', async () => {
    apiMocks.respondToAsk.mockRejectedValueOnce(new Error('Old failure'));
    const { rerender } = render(
      <AskUserPrompt
        request={{ runId: 'run_1', callId: 'call_1', question: 'Old?', multiSelect: false }}
        onResolved={vi.fn()}
      />,
    );
    fireEvent.change(screen.getByLabelText('Your response'), { target: { value: 'Old answer' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send response' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('Old failure');

    rerender(
      <AskUserPrompt
        request={{ runId: 'run_2', callId: 'call_2', question: 'New?', multiSelect: false }}
        onResolved={vi.fn()}
      />,
    );
    expect(screen.getByLabelText('Your response')).toHaveValue('');
    expect(screen.queryByRole('alert')).toBeNull();
  });

  it('ignores completion from a replaced ask while the new request is submitting', async () => {
    let rejectOld!: (error: Error) => void;
    let resolveNew!: () => void;
    apiMocks.respondToAsk
      .mockReturnValueOnce(
        new Promise<void>((_resolve, reject) => {
          rejectOld = reject;
        }),
      )
      .mockReturnValueOnce(
        new Promise<void>((resolve) => {
          resolveNew = resolve;
        }),
      );
    const onResolved = vi.fn();
    const { rerender } = render(
      <AskUserPrompt
        request={{ runId: 'run_1', callId: 'call_1', question: 'Old?', multiSelect: false }}
        onResolved={onResolved}
      />,
    );
    fireEvent.change(screen.getByLabelText('Your response'), { target: { value: 'Old answer' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send response' }));
    rerender(
      <AskUserPrompt
        request={{ runId: 'run_2', callId: 'call_2', question: 'New?', multiSelect: false }}
        onResolved={onResolved}
      />,
    );
    fireEvent.change(screen.getByLabelText('Your response'), { target: { value: 'New answer' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send response' }));

    rejectOld(new Error('Old failure'));
    await waitFor(() => expect(apiMocks.respondToAsk).toHaveBeenCalledTimes(2));
    expect(screen.queryByRole('alert')).toBeNull();
    expect(screen.getByRole('button', { name: 'Sending...' })).toBeDisabled();

    resolveNew();
    await waitFor(() => expect(onResolved).toHaveBeenCalledWith('call_2'));
  });

  it.each([
    ['Allow', 'allow'],
    ['Deny', 'deny'],
  ] as const)('resolves a creation request once via %s', async (button, decision) => {
    apiMocks.resolveCreation.mockResolvedValue({ ok: true });
    const onResolved = vi.fn();
    render(
      <CreationRequestDialog
        request={{
          id: 'creation_1',
          runId: 'run_1',
          toolName: 'create_scene',
          preview: { title: 'Arrival' },
        }}
        onResolved={onResolved}
      />,
    );
    fireEvent.click(screen.getByRole('button', { name: button }));

    await waitFor(() => expect(onResolved).toHaveBeenCalledOnce());
    expect(onResolved).toHaveBeenCalledWith('creation_1');
    expect(apiMocks.resolveCreation).toHaveBeenCalledOnce();
    expect(apiMocks.resolveCreation).toHaveBeenCalledWith(
      'creation_1',
      decision,
      decision === 'allow' ? { title: 'Arrival' } : undefined,
    );
  });

  it('resets creation edits and errors when request identity changes', async () => {
    apiMocks.resolveCreation.mockRejectedValueOnce(new Error('Old failure'));
    const { rerender } = render(
      <CreationRequestDialog
        request={{
          id: 'creation_1',
          runId: 'run_1',
          toolName: 'create_scene',
          preview: { title: 'Old' },
        }}
        onResolved={vi.fn()}
      />,
    );
    fireEvent.click(screen.getByRole('button', { name: 'Allow' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('Old failure');
    rerender(
      <CreationRequestDialog
        request={{
          id: 'creation_2',
          runId: 'run_2',
          toolName: 'create_scene',
          preview: { title: 'New' },
        }}
        onResolved={vi.fn()}
      />,
    );
    expect(screen.getByLabelText('Proposed values')).toHaveValue('{\n  "title": "New"\n}');
    expect(screen.queryByRole('alert')).toBeNull();
  });

  it('ignores completion from a replaced creation while the new request is submitting', async () => {
    let rejectOld!: (error: Error) => void;
    let resolveNew!: () => void;
    apiMocks.resolveCreation
      .mockReturnValueOnce(
        new Promise<void>((_resolve, reject) => {
          rejectOld = reject;
        }),
      )
      .mockReturnValueOnce(
        new Promise<void>((resolve) => {
          resolveNew = resolve;
        }),
      );
    const onResolved = vi.fn();
    const { rerender } = render(
      <CreationRequestDialog
        request={{ id: 'creation_1', runId: 'run_1', toolName: 'create_scene' }}
        onResolved={onResolved}
      />,
    );
    fireEvent.click(screen.getByRole('button', { name: 'Allow' }));
    rerender(
      <CreationRequestDialog
        request={{ id: 'creation_2', runId: 'run_2', toolName: 'create_scene' }}
        onResolved={onResolved}
      />,
    );
    fireEvent.click(screen.getByRole('button', { name: 'Allow' }));

    rejectOld(new Error('Old failure'));
    await waitFor(() => expect(apiMocks.resolveCreation).toHaveBeenCalledTimes(2));
    expect(screen.queryByRole('alert')).toBeNull();
    expect(screen.getByRole('button', { name: 'Submitting...' })).toBeDisabled();

    resolveNew();
    await waitFor(() => expect(onResolved).toHaveBeenCalledWith('creation_2'));
  });

  it.each([
    ['ask', 'Agent question'],
    ['creation', 'Creation request'],
  ] as const)('traps and restores focus for the %s dialog', (_kind, title) => {
    const opener = document.createElement('button');
    document.body.append(opener);
    opener.focus();
    const view =
      title === 'Agent question'
        ? render(
            <AskUserPrompt
              request={{
                runId: 'run_1',
                callId: 'call_1',
                question: 'Choose',
                choices: ['One'],
                multiSelect: false,
              }}
              onResolved={vi.fn()}
            />,
          )
        : render(
            <CreationRequestDialog
              request={{
                id: 'creation_1',
                runId: 'run_1',
                toolName: 'create_scene',
                preview: { title: 'One' },
              }}
              onResolved={vi.fn()}
            />,
          );
    const dialog = screen.getByRole('dialog', { name: title });
    const focusable = dialog.querySelectorAll<HTMLElement>(
      'button:not([disabled]), textarea:not([disabled])',
    );
    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    last.focus();
    fireEvent.keyDown(dialog, { key: 'Tab' });
    expect(document.activeElement).toBe(first);
    first.focus();
    fireEvent.keyDown(dialog, { key: 'Tab', shiftKey: true });
    expect(document.activeElement).toBe(last);
    view.unmount();
    expect(document.activeElement).toBe(opener);
    opener.remove();
  });

  it('keeps the creation dialog open when resolution fails', async () => {
    apiMocks.resolveCreation.mockRejectedValue(new Error('Creation expired'));
    const onResolved = vi.fn();
    render(
      <CreationRequestDialog
        request={{ id: 'creation_1', runId: 'run_1', toolName: 'create_scene' }}
        onResolved={onResolved}
      />,
    );
    fireEvent.click(screen.getByRole('button', { name: 'Allow' }));

    expect(await screen.findByRole('alert')).toHaveTextContent('Creation expired');
    expect(onResolved).not.toHaveBeenCalled();
  });
});
