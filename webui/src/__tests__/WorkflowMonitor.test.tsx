import { useEffect } from 'react';
import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { activatePipelineWorkflow, listPipelineWorkflows } from '../api/client';
import type { WorkflowSummary } from '../api/types';
import { AppStateProvider, useAppState } from '../AppState';
import WorkflowMonitor from '../components/Sidebar/WorkflowMonitor';

vi.mock('../api/client', () => ({
  listPipelineWorkflows: vi.fn(),
  activatePipelineWorkflow: vi.fn(),
}));

function MonitorHarness() {
  const { dispatch } = useAppState();

  useEffect(() => {
    dispatch({ type: 'SET_WORLD', worldId: 'world_1' });
  }, [dispatch]);

  return <WorkflowMonitor />;
}

const MOCK_WORKFLOWS: WorkflowSummary[] = [
  { name: 'default', description: 'Standard pipeline', version: 1, phase_count: 5 },
  { name: 'fast_track', description: 'Fast track pipeline', version: 1, phase_count: 3 },
];

const SINGLE_WORKFLOW: WorkflowSummary[] = [
  { name: 'default', description: 'Standard pipeline', version: 1, phase_count: 5 },
];

describe('WorkflowMonitor', () => {
  let alertMock: ReturnType<typeof vi.fn>;

  beforeEach(() => {
    vi.clearAllMocks();
    alertMock = vi.fn();
    vi.stubGlobal('alert', alertMock);
  });

  it('shows loading state initially', async () => {
    vi.mocked(listPipelineWorkflows).mockReturnValue(new Promise<WorkflowSummary[]>(() => {}));

    render(
      <AppStateProvider>
        <MonitorHarness />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByText('Loading workflows...')).toBeDefined();
    });
  });

  it('renders workflow selector when multiple workflows loaded', async () => {
    vi.mocked(listPipelineWorkflows).mockResolvedValue(MOCK_WORKFLOWS);

    render(
      <AppStateProvider>
        <MonitorHarness />
      </AppStateProvider>,
    );

    expect(await screen.findByRole('combobox')).toBeDefined();
  });

  it('shows error state when API fails', async () => {
    vi.mocked(listPipelineWorkflows).mockRejectedValue(new Error('Network failure'));

    render(
      <AppStateProvider>
        <MonitorHarness />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByText(/Error: Network failure/)).toBeDefined();
    });
  });

  it('hides selector when only 1 workflow', async () => {
    vi.mocked(listPipelineWorkflows).mockResolvedValue(SINGLE_WORKFLOW);

    render(
      <AppStateProvider>
        <MonitorHarness />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByText('Workflow Status')).toBeDefined();
    });

    expect(screen.queryByRole('combobox')).toBeNull();
  });

  it('calls activatePipelineWorkflow on selection change', async () => {
    vi.mocked(listPipelineWorkflows).mockResolvedValue(MOCK_WORKFLOWS);
    vi.mocked(activatePipelineWorkflow).mockResolvedValue();

    render(
      <AppStateProvider>
        <MonitorHarness />
      </AppStateProvider>,
    );

    const select = await screen.findByRole('combobox');

    fireEvent.change(select, { target: { value: 'fast_track' } });

    await waitFor(() => {
      expect(vi.mocked(activatePipelineWorkflow)).toHaveBeenCalledWith('world_1', 'fast_track');
    });
  });

  it('reverts selection on activation failure', async () => {
    vi.mocked(listPipelineWorkflows).mockResolvedValue(MOCK_WORKFLOWS);
    vi.mocked(activatePipelineWorkflow).mockRejectedValue(new Error('Server error'));

    render(
      <AppStateProvider>
        <MonitorHarness />
      </AppStateProvider>,
    );

    const select = await screen.findByRole('combobox');
    expect((select as HTMLSelectElement).value).toBe('default');

    fireEvent.change(select, { target: { value: 'fast_track' } });

    await waitFor(() => {
      expect(alertMock).toHaveBeenCalledWith('Failed to activate workflow: Server error');
      expect((select as HTMLSelectElement).value).toBe('default');
    });
  });
});
