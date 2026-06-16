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
  }, [allowedRetreat, dispatch, nextAllowed, phase, pipelineAdvanceError]);

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

  it('renders localized phase list', () => {
    render(
      <AppStateProvider>
        <PipelineHarness />
      </AppStateProvider>,
    );

    expect(screen.getByText('世界观构建')).toBeInTheDocument();
    expect(screen.getByText('角色创建')).toBeInTheDocument();
    expect(screen.getByText('情节架构')).toBeInTheDocument();
    expect(screen.getByText('场景写作')).toBeInTheDocument();
    expect(screen.getByText('复盘修订')).toBeInTheDocument();
  });

  it('shows localized error banner when pipelineAdvanceError is set', () => {
    render(
      <AppStateProvider>
        <PipelineHarness pipelineAdvanceError="Network error" />
      </AppStateProvider>,
    );

    expect(screen.getByText((content, node) => node?.textContent === '推进失败：Network error')).toBeInTheDocument();
  });

  it('dismiss button clears error', async () => {
    render(
      <AppStateProvider>
        <PipelineHarness pipelineAdvanceError="Test error" />
      </AppStateProvider>,
    );

    const errorBanner = screen.getByText((content, node) => node?.textContent === '推进失败：Test error').parentElement!;

    fireEvent.click(within(errorBanner).getByText('关闭'));

    await waitFor(() => {
      expect(screen.queryByText((content, node) => node?.textContent === '推进失败：Test error')).toBeNull();
    });
  });

  it('shows localized alert on advance failure', async () => {
    vi.mocked(advancePipeline).mockRejectedValue(new Error('Server error'));
    confirmMock.mockReturnValue(true);

    render(
      <AppStateProvider>
        <PipelineHarness phase="worldbuilding" nextAllowed={NEXT_CHAR_CREATION} />
      </AppStateProvider>,
    );

    fireEvent.click(screen.getByText('角色创建'));

    await waitFor(() => {
      expect(alertMock).toHaveBeenCalledWith('阶段推进失败：Server error');
    });
  });
});
