import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import UserCell from '../components/cells/UserCell';
import AssistantCell from '../components/cells/AssistantCell';
import SystemCell from '../components/cells/SystemCell';
import StatusPill from '../components/cells/StatusPill';
import ToolCell from '../components/cells/ToolCell';

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
