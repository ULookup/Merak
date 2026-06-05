import { useEffect } from 'react';
import { render, screen } from '@testing-library/react';
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

  it('UserCell renders text', () => {
    render(<UserCell text="Hello world" />);
    expect(screen.getByText('Hello world')).toBeDefined();
  });

  it('AssistantCell renders markdown', () => {
    render(<AssistantCell text="**bold**" />);
    expect(screen.getByText('bold')).toBeDefined();
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
});
