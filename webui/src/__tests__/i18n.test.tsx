import { render, screen } from '@testing-library/react';
import { fireEvent } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { I18nProvider, LanguageToggle, useI18n } from '../i18n';
import HelpDrawer from '../components/HelpDrawer';
import WorldOnboarding from '../components/WorldOnboarding';
import { AppStateProvider } from '../AppState';

function Probe() {
  const { t } = useI18n();
  return <div>{t('app.workbench')}</div>;
}

describe('i18n polish', () => {
  it('defaults the workbench chrome to Chinese for first-time desktop users', () => {
    render(
      <I18nProvider defaultLocale="zh">
        <Probe />
      </I18nProvider>,
    );

    expect(screen.getByText('创作工作台')).toBeDefined();
  });

  it('switches between Chinese and English without leaving the page', async () => {
    render(
      <I18nProvider defaultLocale="zh">
        <Probe />
        <LanguageToggle />
      </I18nProvider>,
    );

    fireEvent.click(screen.getByRole('button', { name: 'Switch language' }));

    expect(screen.getByText('Workbench')).toBeDefined();
  });

  it('offers a plain-language help guide for non-technical authors', () => {
    render(
      <I18nProvider defaultLocale="zh">
        <HelpDrawer open={true} onClose={() => {}} />
      </I18nProvider>,
    );

    expect(screen.getByRole('dialog', { name: '使用帮助' })).toBeDefined();
    expect(screen.getByText('和 GodAgent 说你想写什么')).toBeDefined();
    expect(screen.queryByText(/API|SSE|runtime/i)).toBeNull();
  });

  it('localizes the first-run world creation screen', () => {
    render(
      <I18nProvider defaultLocale="zh">
        <AppStateProvider>
          <WorldOnboarding onOpenGuide={() => {}} />
        </AppStateProvider>
      </I18nProvider>,
    );

    expect(screen.getByText('Merak 创作工作室')).toBeDefined();
    expect(screen.getByLabelText('世界名称')).toBeDefined();
    expect(screen.getByRole('button', { name: '打开使用帮助' })).toBeDefined();
  });
});
