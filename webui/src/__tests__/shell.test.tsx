import { fireEvent, render, screen, within } from '@testing-library/react';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { AppStateProvider } from '../AppState';
import { I18nProvider } from '../i18n';
import DesktopShell from '../shell/DesktopShell';
import { desktopPages, readStoredDesktopPage, writeStoredDesktopPage } from '../shell/navigation';

describe('desktop shell', () => {
  beforeEach(() => window.localStorage.clear());

  function renderShell(page: 'overview' | 'sessions', onNavigate = vi.fn()) {
    return render(
      <I18nProvider defaultLocale="zh">
        <AppStateProvider>
          <DesktopShell page={page} onNavigate={onNavigate}>
            <div>Existing workbench</div>
          </DesktopShell>
        </AppStateProvider>
      </I18nProvider>,
    );
  }

  it('renders exactly ten bilingual navigation buttons and dispatches navigation', () => {
    const dispatch = vi.fn();
    renderShell('overview', (page) => dispatch({ type: 'SET_PAGE', page }));

    const navigation = screen.getByRole('navigation', { name: '主导航' });
    expect(within(navigation).getAllByRole('button')).toHaveLength(10);
    expect(within(navigation).getByRole('button', { name: '概览' })).toBeInTheDocument();
    expect(within(navigation).getByRole('button', { name: 'Sessions 会话' })).toBeInTheDocument();
    expect(within(navigation).getByRole('button', { name: 'World 世界设定' })).toBeInTheDocument();

    fireEvent.click(within(navigation).getByRole('button', { name: /角色/ }));
    expect(dispatch).toHaveBeenCalledWith({ type: 'SET_PAGE', page: 'characters' });
  });

  it('marks the active page and preserves the existing workbench children', () => {
    renderShell('sessions');

    expect(screen.getByRole('button', { name: 'Sessions 会话' })).toHaveAttribute(
      'aria-current',
      'page',
    );
    expect(screen.getByText('Existing workbench')).toBeInTheDocument();
  });

  it('restores only persisted top-level pages', () => {
    window.localStorage.setItem('merak.desktop.page', 'characters');
    expect(readStoredDesktopPage()).toBe('characters');

    window.localStorage.setItem('merak.desktop.page', 'editor');
    expect(readStoredDesktopPage()).toBe('overview');

    window.localStorage.setItem('merak.desktop.page', 'not-a-page');
    expect(readStoredDesktopPage()).toBe('overview');
  });

  it('persists only validated top-level pages', () => {
    writeStoredDesktopPage('files');
    expect(window.localStorage.getItem('merak.desktop.page')).toBe('files');

    writeStoredDesktopPage('editor');
    expect(window.localStorage.getItem('merak.desktop.page')).toBe('files');
    expect(desktopPages).toHaveLength(10);
  });
});
