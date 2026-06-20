import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { api } from '../api/client';
import { AppStateProvider } from '../AppState';
import SettingsPanel from '../components/Sidebar/SettingsPanel';
import { I18nProvider } from '../i18n';
import SettingsPage from '../pages/SettingsPage';

vi.mock('../api/client', () => ({
  api: {
    getConfig: vi.fn().mockResolvedValue({
      provider: 'openai',
      api_key_masked: 'sk-...abcd',
      default_model: 'gpt-4o',
      api_base_url: 'https://api.openai.com/v1',
      max_output_tokens: 4096,
    }),
    saveConfig: vi.fn().mockResolvedValue({ ok: true }),
    testConfig: vi.fn().mockResolvedValue({ ok: true }),
    getPreferences: vi.fn().mockResolvedValue({
      default_genre: 'No preference',
      preferred_style: 'concise',
      allow_usage_logs: true,
    }),
    savePreferences: vi.fn().mockResolvedValue({ ok: true }),
    metadata: vi.fn().mockResolvedValue({
      provider: 'openai',
      model: 'gpt-4o',
      models: [],
      permission_mode: 'approval',
      memory: { enabled: true },
      tools: [],
      mcp_servers: [],
      agents: [],
      delegation_patterns: [],
    }),
  },
  formatApiError: vi.fn((error: unknown, fallback: string) =>
    error instanceof Error ? error.message : fallback,
  ),
}));

vi.mock('../desktop', () => ({
  exportDiagnostics: vi.fn().mockResolvedValue({ ok: true, path: 'C:/Users/me/report.zip' }),
  getDesktopRuntimeStatus: vi.fn().mockResolvedValue({
    phase: 'ready',
    apiBaseUrl: 'http://127.0.0.1:3888',
    port: 3888,
    pid: 1234,
    version: '0.1.0',
    pgStatus: 'ready',
    configPath: 'C:/Users/me/.merak/settings.local.json',
    logPath: 'C:/Users/me/.merak/logs/desktop.log',
    error: null,
  }),
  isDesktopApp: vi.fn(() => true),
  openDiagnosticsFolder: vi.fn(),
  restartDesktopRuntime: vi.fn().mockResolvedValue({ ok: true, status: null }),
}));

describe('SettingsPanel polish', () => {
  it('uses plain Chinese labels for model and desktop settings', async () => {
    render(
      <I18nProvider defaultLocale="zh">
        <SettingsPanel />
      </I18nProvider>,
    );

    expect(await screen.findByRole('heading', { name: '创作助手设置' })).toBeDefined();
    expect(screen.getByLabelText('访问密钥')).toBeDefined();
    expect(screen.getByText('本地桌面状态')).toBeDefined();
    expect(screen.getByRole('button', { name: '导出故障报告' })).toBeDefined();
    expect(document.body.textContent ?? '').not.toMatch(
      /API Key|API Base URL|Runtime|Database|Diagnostics/i,
    );
  });
});

describe('Settings page capabilities', () => {
  it('keeps API credentials masked and does not enable unsupported settings', async () => {
    render(
      <AppStateProvider>
        <SettingsPage />
      </AppStateProvider>,
    );

    const key = await screen.findByLabelText('API key');
    expect(key).toHaveAttribute('type', 'password');
    expect(screen.queryByRole('switch', { name: /auto.?save/i })).toBeNull();
    expect(screen.queryByRole('combobox', { name: /theme/i })).toBeNull();
    expect(screen.queryByRole('textbox', { name: /database/i })).toBeNull();
  });

  it('renders writable API base URL, read-only permission mode, and restart-required save result', async () => {
    vi.mocked(api.saveConfig).mockResolvedValueOnce({ ok: true, restart_required: true } as never);
    render(
      <AppStateProvider>
        <SettingsPage />
      </AppStateProvider>,
    );
    expect(await screen.findByLabelText('API base URL')).toHaveValue('https://api.openai.com/v1');
    expect(await screen.findByText('approval')).toBeDefined();
    fireEvent.change(screen.getByLabelText('API base URL'), {
      target: { value: 'http://localhost:11434/v1' },
    });
    fireEvent.click(screen.getByRole('button', { name: 'Save model settings' }));
    expect(await screen.findByRole('status')).toHaveTextContent(/restart required/i);
    expect(api.saveConfig).toHaveBeenCalledWith(
      expect.objectContaining({ api_base_url: 'http://localhost:11434/v1' }),
    );
  });
});
