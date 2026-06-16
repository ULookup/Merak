import { type ReactNode, useEffect, useMemo, useState } from 'react';
import { CheckCircle2, Circle, CircleAlert, Loader2 } from 'lucide-react';
import { api, formatApiError, setApiBase } from './api/client';
import {
  exportDiagnostics,
  getDesktopRuntimeLogs,
  getDesktopRuntimeStatus,
  isDesktopApp,
  openDiagnosticsFolder,
  restartDesktopRuntime,
  type DesktopRuntimeStatus,
} from './desktop';
import { LanguageToggle, useI18n } from './i18n';
import styles from './DesktopBoot.module.css';

interface Props {
  children: ReactNode;
}

type BootPhase = 'checking' | 'starting' | 'configuring' | 'ready' | 'failed';
type BootStepState = 'done' | 'active' | 'waiting' | 'failed';
type Translate = (key: string) => string;

const defaultStepCopy: Record<string, string> = {
  'settings.process': 'Local process',
  'desktop.failedTitle': 'Merak needs your attention',
  'desktop.localAddress': 'Local address',
  'desktop.selectingPort': 'Selecting local port',
  'desktop.assistant': 'Writing assistant',
  'desktop.checking': 'Checking',
  'app.workbench': 'Workbench',
  'desktop.preparingReport': 'Preparing report',
  'desktop.startingTitle': 'Opening Merak',
  'desktop.accessKeyPlaceholder': 'Paste your access key',
  'desktop.readyTitle': 'Merak is ready',
  'desktop.startingCopy': 'Merak is preparing the local creative workspace.',
};

interface BootStep {
  label: string;
  detail: string;
  state: BootStepState;
}

interface ConfigForm {
  provider: string;
  apiKey: string;
  model: string;
  baseUrl: string;
}

function needsFirstRunConfig(apiKeyMasked?: string) {
  return !apiKeyMasked || apiKeyMasked.endsWith('here');
}

export function getDesktopRuntimeSteps(
  phase: BootPhase,
  runtime: DesktopRuntimeStatus | null,
  t: Translate = (key) => defaultStepCopy[key] ?? key,
): BootStep[] {
  if (phase === 'failed') {
    return [
      {
        label: t('settings.process'),
        detail: runtime?.error ?? t('desktop.failedTitle'),
        state: 'failed',
      },
      {
        label: t('desktop.localAddress'),
        detail: runtime?.apiBaseUrl ?? t('desktop.selectingPort'),
        state: 'waiting',
      },
      {
        label: t('desktop.assistant'),
        detail: t('desktop.checking'),
        state: 'waiting',
      },
      {
        label: t('app.workbench'),
        detail: t('desktop.preparingReport'),
        state: 'waiting',
      },
    ];
  }

  const runtimeDone = phase === 'configuring' || phase === 'ready' || runtime?.phase === 'ready';
  const apiDone = Boolean(runtime?.apiBaseUrl) && (phase === 'configuring' || phase === 'ready');
  const configActive = phase === 'configuring';

  return [
    {
      label: t('settings.process'),
      detail: runtime?.pid ? `PID ${runtime.pid}` : t('desktop.startingTitle'),
      state: runtimeDone ? 'done' : phase === 'starting' ? 'active' : 'waiting',
    },
    {
      label: t('desktop.localAddress'),
      detail: runtime?.apiBaseUrl ?? t('desktop.selectingPort'),
      state: apiDone ? 'done' : runtimeDone ? 'active' : 'waiting',
    },
    {
      label: t('desktop.assistant'),
      detail: configActive ? t('desktop.accessKeyPlaceholder') : t('desktop.checking'),
      state: phase === 'ready' ? 'done' : configActive ? 'active' : 'waiting',
    },
    {
      label: t('app.workbench'),
      detail: phase === 'ready' ? t('desktop.readyTitle') : t('desktop.startingCopy'),
      state: phase === 'ready' ? 'done' : 'waiting',
    },
  ];
}

function StepIcon({ state }: { state: BootStepState }) {
  if (state === 'done') return <CheckCircle2 size={17} aria-hidden="true" />;
  if (state === 'failed') return <CircleAlert size={17} aria-hidden="true" />;
  if (state === 'active') return <Loader2 size={17} aria-hidden="true" />;
  return <Circle size={17} aria-hidden="true" />;
}

export default function DesktopBoot({ children }: Props) {
  const { t } = useI18n();
  const desktop = isDesktopApp();
  const [phase, setPhase] = useState<BootPhase>(desktop ? 'checking' : 'ready');
  const [runtime, setRuntime] = useState<DesktopRuntimeStatus | null>(null);
  const [logs, setLogs] = useState<string[]>([]);
  const [message, setMessage] = useState('');
  const [form, setForm] = useState<ConfigForm>({
    provider: 'openai',
    apiKey: '',
    model: 'gpt-4o',
    baseUrl: 'https://api.openai.com/v1',
  });
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    if (!desktop) return;
    let cancelled = false;
    let attempts = 0;

    async function poll() {
      const status = await getDesktopRuntimeStatus();
      if (cancelled) return;
      setRuntime(status);

      if (status?.apiBaseUrl) {
        setApiBase(status.apiBaseUrl);
      }

      if (status?.phase === 'ready' && status.apiBaseUrl) {
        try {
          const config = await api.getConfig();
          if (cancelled) return;
          if (needsFirstRunConfig(config.api_key_masked)) {
            setForm({
              provider: config.provider || 'openai',
              apiKey: '',
              model: config.default_model || 'gpt-4o',
              baseUrl: config.api_base_url || 'https://api.openai.com/v1',
            });
            setPhase('configuring');
          } else {
            setPhase('ready');
          }
        } catch {
          setPhase('ready');
        }
        return;
      }

      if (status?.phase === 'failed') {
        setPhase('failed');
        const response = await getDesktopRuntimeLogs(80);
        if (!cancelled) setLogs(response?.lines ?? []);
        return;
      }

      setPhase(status?.phase === 'starting' ? 'starting' : 'checking');
      attempts += 1;
      window.setTimeout(poll, attempts < 8 ? 700 : 1500);
    }

    poll();
    return () => {
      cancelled = true;
    };
  }, [desktop]);

  const statusLabel = useMemo(() => {
    if (!runtime) return t('desktop.preparingTitle');
    if (runtime.phase === 'ready') return t('desktop.readyTitle');
    if (runtime.phase === 'failed') return t('desktop.failedTitle');
    return t('desktop.startingTitle');
  }, [runtime, t]);

  const steps = useMemo(() => getDesktopRuntimeSteps(phase, runtime, t), [phase, runtime, t]);

  async function handleRestart() {
    setPhase('starting');
    setMessage('');
    const response = await restartDesktopRuntime();
    setRuntime(response?.status ?? null);
    if (response?.status.apiBaseUrl) setApiBase(response.status.apiBaseUrl);
    window.setTimeout(() => window.location.reload(), 500);
  }

  async function handleExportDiagnostics() {
    const response = await exportDiagnostics();
    if (response?.ok) setMessage(`${t('desktop.exported')} ${response.path}`);
    else if (response) setMessage(response.path);
  }

  async function handleSaveConfig() {
    setSaving(true);
    setMessage('');
    try {
      await api.saveConfig({
        provider: form.provider,
        api_key: form.apiKey,
        default_model: form.model,
        api_base_url: form.baseUrl,
      });
      const response = await restartDesktopRuntime();
      if (response?.status.apiBaseUrl) setApiBase(response.status.apiBaseUrl);
      setPhase(response?.ok ? 'ready' : 'failed');
      setRuntime(response?.status ?? null);
    } catch (error) {
      setMessage(formatApiError(error, t('desktop.configError')));
    } finally {
      setSaving(false);
    }
  }

  if (!desktop || phase === 'ready') {
    return <>{children}</>;
  }

  return (
    <main className={styles.shell}>
      <section className={styles.panel} aria-live="polite">
        <div className={styles.topbar}>
          <div className={styles.brand}>MERAK</div>
          <div className={styles.languageToggle}>
            <LanguageToggle />
          </div>
        </div>
        <h1>{phase === 'configuring' ? t('desktop.connectTitle') : statusLabel}</h1>
        {phase !== 'configuring' && (
          <p>{t('desktop.startingCopy')}</p>
        )}

        <ol className={styles.stepRail} aria-label="Desktop startup progress">
          {steps.map((step) => (
            <li key={step.label} className={`${styles.step} ${styles[`step_${step.state}`]}`}>
              <div className={styles.stepIcon}>
                <StepIcon state={step.state} />
              </div>
              <div>
                <strong>{step.label}</strong>
                <span>{step.detail}</span>
              </div>
            </li>
          ))}
        </ol>

        {phase === 'configuring' ? (
          <div className={styles.form}>
            <label>
              {t('desktop.service')}
              <select
                value={form.provider}
                onChange={(event) => setForm((prev) => ({ ...prev, provider: event.target.value }))}
              >
                <option value="openai">OpenAI</option>
                <option value="anthropic">Anthropic</option>
                <option value="custom">Custom</option>
              </select>
            </label>
            <label>
              {t('desktop.accessKey')}
              <input
                type="password"
                value={form.apiKey}
                onChange={(event) => setForm((prev) => ({ ...prev, apiKey: event.target.value }))}
                placeholder={t('desktop.accessKeyPlaceholder')}
              />
            </label>
            <label>
              {t('desktop.assistant')}
              <input
                value={form.model}
                onChange={(event) => setForm((prev) => ({ ...prev, model: event.target.value }))}
                placeholder="gpt-4o"
              />
            </label>
            <label>
              {t('desktop.advancedAddress')}
              <input
                value={form.baseUrl}
                onChange={(event) => setForm((prev) => ({ ...prev, baseUrl: event.target.value }))}
                placeholder="https://api.openai.com/v1"
              />
            </label>
            <button
              className={styles.primary}
              type="button"
              onClick={handleSaveConfig}
              disabled={saving || form.apiKey.trim().length === 0}
            >
              {saving ? t('desktop.saving') : t('desktop.save')}
            </button>
          </div>
        ) : (
          <div className={styles.statusGrid}>
            <span>{t('desktop.step')}</span>
            <strong>{runtime?.phase ?? phase}</strong>
            <span>{t('desktop.localAddress')}</span>
            <strong>{runtime?.apiBaseUrl ?? t('desktop.selectingPort')}</strong>
            <span>{t('desktop.localLibrary')}</span>
            <strong>{runtime?.pgStatus ?? t('desktop.checking')}</strong>
            <span>{t('desktop.reportFolder')}</span>
            <strong>{runtime?.logPath ?? t('desktop.preparingReport')}</strong>
          </div>
        )}

        {phase === 'failed' && (
          <>
            <div className={styles.actions}>
              <button type="button" onClick={handleRestart}>
                {t('desktop.restart')}
              </button>
              <button type="button" onClick={() => openDiagnosticsFolder()}>
                {t('desktop.openReports')}
              </button>
              <button type="button" onClick={handleExportDiagnostics}>
                {t('desktop.exportReport')}
              </button>
            </div>
            <pre className={styles.log}>{logs.slice(-12).join('\n')}</pre>
          </>
        )}

        {runtime?.error && <div className={styles.error}>{runtime.error}</div>}
        {message && <div className={styles.note}>{message}</div>}
      </section>
    </main>
  );
}
