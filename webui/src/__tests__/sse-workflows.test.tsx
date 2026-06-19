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
    expect(apiMocks.respondToAsk).toHaveBeenCalledWith('run_1', 'First person', 'call_1');
    expect(onResolved).not.toHaveBeenCalled();
    expect(screen.getByRole('button', { name: 'Sending...' })).toBeDisabled();

    complete();
    await waitFor(() => expect(onResolved).toHaveBeenCalledOnce());
  });

  it('keeps the ask dialog open and formats request errors', async () => {
    apiMocks.respondToAsk.mockRejectedValue(new Error('Run is no longer waiting'));
    const onResolved = vi.fn();
    render(
      <AskUserPrompt request={{ runId: 'run_1', question: 'Continue?' }} onResolved={onResolved} />,
    );
    fireEvent.change(screen.getByLabelText('Your response'), { target: { value: 'Yes' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send response' }));

    expect(await screen.findByRole('alert')).toHaveTextContent('Run is no longer waiting');
    expect(onResolved).not.toHaveBeenCalled();
  });

  it.each([
    ['Allow', 'allow'],
    ['Deny', 'deny'],
  ] as const)('resolves a creation request once via %s', async (button, decision) => {
    apiMocks.resolveCreation.mockResolvedValue({ ok: true });
    const onResolved = vi.fn();
    render(
      <CreationRequestDialog
        request={{ id: 'creation_1', toolName: 'create_scene', preview: { title: 'Arrival' } }}
        onResolved={onResolved}
      />,
    );
    fireEvent.click(screen.getByRole('button', { name: button }));

    await waitFor(() => expect(onResolved).toHaveBeenCalledOnce());
    expect(apiMocks.resolveCreation).toHaveBeenCalledOnce();
    expect(apiMocks.resolveCreation).toHaveBeenCalledWith(
      'creation_1',
      decision,
      decision === 'allow' ? { title: 'Arrival' } : undefined,
    );
  });

  it('keeps the creation dialog open when resolution fails', async () => {
    apiMocks.resolveCreation.mockRejectedValue(new Error('Creation expired'));
    const onResolved = vi.fn();
    render(
      <CreationRequestDialog
        request={{ id: 'creation_1', toolName: 'create_scene' }}
        onResolved={onResolved}
      />,
    );
    fireEvent.click(screen.getByRole('button', { name: 'Allow' }));

    expect(await screen.findByRole('alert')).toHaveTextContent('Creation expired');
    expect(onResolved).not.toHaveBeenCalled();
  });
});
