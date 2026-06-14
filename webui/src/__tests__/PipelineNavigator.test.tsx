import { useEffect, useRef } from 'react';
import { describe, expect, it, vi, beforeEach } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { AppStateProvider, useAppState } from '../AppState';
import type { CreativePhase } from '../api/types';

vi.mock('../api/client', () => ({
  advancePipeline: vi.fn(),
  getPipelineState: vi.fn().mockResolvedValue({}),
}));

import PipelineNavigator from '../components/Sidebar/PipelineNavigator';
import { advancePipeline } from '../api/client';

const EMPTY_ARR: string[] = [];
const NEXT_CHAR_CREATION: CreativePhase[] = ['character_creation'];

function PipelineHarness({
  phase = 'worldbuilding',
  nextAllowed = EMPTY_ARR,
  allowedRetreat = EMPTY_ARR,
  pipelineAdvanceError,
}: {
  phase?: CreativePhase;
  nextAllowed?: string[];
  allowedRetreat?: string[];
  pipelineAdvanceError?: string;
} = {}) {
  const { dispatch } = useAppState();
  const didInit = useRef(false);

  useEffect(() => {
    if (didInit.current) return;
    didInit.current = true;
    dispatch({ type: 'SET_WORLD', worldId: 'world_1' });
    dispatch({
      type: 'SET_PIPELINE_VIEW',
      view: {
        phase,
        next_allowed: nextAllowed as CreativePhase[],
        allowed_retreat: allowedRetreat as CreativePhase[],
        conditions: [],
        active_workflow: '',
        recent_history: [],
      },
    });
    if (pipelineAdvanceError) {
      dispatch({ type: 'PIPELINE_ADVANCE_FAILED', reason: pipelineAdvanceError });
    }
  }, []);

  return <PipelineNavigator />;
}

describe('PipelineNavigator', () => {
  let alertMock: ReturnType<typeof vi.fn>;
  let confirmMock: ReturnType<typeof vi.fn>;

  beforeEach(() => {
    vi.clearAllMocks();
    confirmMock = vi.fn(() => false);
    alertMock = vi.fn();
    vi.stubGlobal('confirm', confirmMock);
    vi.stubGlobal('alert', alertMock);
  });

  it('renders phase list', () => {
    render(
      <AppStateProvider>
        <PipelineHarness />
      </AppStateProvider>,
    );

    expect(screen.getByText('世界观构建')).toBeInTheDocument();
    expect(screen.getByText('角色创建')).toBeInTheDocument();
    expect(screen.getByText('剧情架构')).toBeInTheDocument();
    expect(screen.getByText('场景写作')).toBeInTheDocument();
    expect(screen.getByText('回顾整理')).toBeInTheDocument();
  });

  it('shows error banner when pipelineAdvanceError is set', () => {
    render(
      <AppStateProvider>
        <PipelineHarness pipelineAdvanceError="Network error" />
      </AppStateProvider>,
    );

    expect(screen.getByText(/Advance failed: Network error/)).toBeInTheDocument();
  });

  it('dismiss button clears error', async () => {
    render(
      <AppStateProvider>
        <PipelineHarness pipelineAdvanceError="Test error" />
      </AppStateProvider>,
    );

    expect(screen.getByText(/Advance failed: Test error/)).toBeInTheDocument();

    fireEvent.click(within(screen.getByText(/Advance failed: Test error/).parentElement!).getByText('Dismiss'));

    await waitFor(() => {
      expect(screen.queryByText(/Advance failed: Test error/)).toBeNull();
    });
  });

  it('shows alert on advance failure', async () => {
    vi.mocked(advancePipeline).mockRejectedValue(new Error('Server error'));
    confirmMock.mockReturnValue(true);

    render(
      <AppStateProvider>
        <PipelineHarness
          phase="worldbuilding"
          nextAllowed={NEXT_CHAR_CREATION}
        />
      </AppStateProvider>,
    );

    fireEvent.click(screen.getByText('角色创建'));

    await waitFor(() => {
      expect(alertMock).toHaveBeenCalledWith(
        'Pipeline advance failed: Server error',
      );
    });
  });
});
