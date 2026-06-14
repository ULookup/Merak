import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import DesktopBoot from '../DesktopBoot';
import { I18nProvider } from '../i18n';

vi.mock('../desktop', () => ({
  exportDiagnostics: vi.fn(),
  getDesktopRuntimeLogs: vi.fn().mockResolvedValue({ lines: [] }),
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
  restartDesktopRuntime: vi.fn().mockResolvedValue({
    ok: true,
    status: {
      phase: 'ready',
      apiBaseUrl: 'http://127.0.0.1:3888',
      port: 3888,
      pid: 1234,
      version: '0.1.0',
      pgStatus: 'ready',
      configPath: 'C:/Users/me/.merak/settings.local.json',
      logPath: 'C:/Users/me/.merak/logs/desktop.log',
      error: null,
    },
  }),
}));

vi.mock('../api/client', () => ({
  api: {
    getConfig: vi.fn().mockResolvedValue({
      provider: 'openai',
      api_key_masked: 'sk-your-api-key-here',
      default_model: 'gpt-4o',
      api_base_url: 'https://api.openai.com/v1',
      max_output_tokens: 4096,
    }),
    saveConfig: vi.fn().mockResolvedValue({ ok: true }),
  },
  formatApiError: vi.fn((error: unknown, fallback: string) =>
    error instanceof Error ? error.message : fallback,
  ),
  setApiBase: vi.fn(),
}));

function renderDesktopBoot(locale: 'zh' | 'en' = 'zh') {
  return render(
    <I18nProvider defaultLocale={locale}>
      <DesktopBoot>
        <div>Workbench ready</div>
      </DesktopBoot>
    </I18nProvider>,
  );
}

describe('DesktopBoot first-run polish', () => {
  it('uses plain Chinese copy for first-time Windows setup', async () => {
    renderDesktopBoot('zh');

    expect(await screen.findByRole('heading', { name: '连接你的创作助手' })).toBeDefined();
    expect(screen.getByLabelText('访问密钥')).toBeDefined();
    expect(screen.getByRole('button', { name: '保存并进入创作工作台' })).toBeDefined();
    expect(document.body.textContent ?? '').not.toMatch(
      /API Key|API Base URL|runtime|Database|Diagnostics/i,
    );
  });

  it('can switch the desktop setup screen to English', async () => {
    renderDesktopBoot('zh');

    const toggle = await screen.findByRole('button', { name: 'Switch language' });
    fireEvent.click(toggle);

    expect(screen.getByRole('heading', { name: 'Connect your writing assistant' })).toBeDefined();
    expect(screen.getByLabelText('Access key')).toBeDefined();
  });
});
