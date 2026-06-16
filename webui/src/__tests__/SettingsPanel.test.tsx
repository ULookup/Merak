import { render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import SettingsPanel from '../components/Sidebar/SettingsPanel';
import { I18nProvider } from '../i18n';

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
    expect(document.body.textContent ?? '').not.toMatch(/API Key|API Base URL|Runtime|Database|Diagnostics/i);
  });
});
