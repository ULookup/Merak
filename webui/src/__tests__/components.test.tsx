import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import AssistantCell from '../components/cells/AssistantCell';
import StatusPill from '../components/cells/StatusPill';
import SystemCell from '../components/cells/SystemCell';
import ToolCell from '../components/cells/ToolCell';
import UserCell from '../components/cells/UserCell';

describe('Cell components', () => {
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
    const source = 'builtin';
    const icon = source === 'mcp' ? 'iconAsk' : 'iconSafe';
    const badge = source === 'mcp' ? 'badgeAsk' : 'badgeSafe';
    expect(icon).toBe('iconSafe');
    expect(badge).toBe('badgeSafe');
  });

  it('mcp tools map to ask icon and badge', () => {
    const source = 'mcp';
    const icon = source === 'mcp' ? 'iconAsk' : 'iconSafe';
    const badge = source === 'mcp' ? 'badgeAsk' : 'badgeSafe';
    expect(icon).toBe('iconAsk');
    expect(badge).toBe('badgeAsk');
  });

  it('unknown source falls back to safe', () => {
    const source = 'unknown';
    const icon = source === 'mcp' ? 'iconAsk' : 'iconSafe';
    expect(icon).toBe('iconSafe');
  });
});
