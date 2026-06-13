import { type ReactNode, useEffect, useMemo, useState } from 'react';
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
import styles from './DesktopBoot.module.css';

interface Props {
  children: ReactNode;
}

type BootPhase = 'checking' | 'starting' | 'configuring' | 'ready' | 'failed';

interface ConfigForm {
  provider: string;
  apiKey: string;
  model: string;
  baseUrl: string;
}

function needsFirstRunConfig(apiKeyMasked?: string) {
  return !apiKeyMasked || apiKeyMasked.endsWith('here');
}

export default function DesktopBoot({ children }: Props) {
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
    if (!runtime) return 'Preparing Merak runtime';
    if (runtime.phase === 'ready') return 'Merak runtime is ready';
    if (runtime.phase === 'failed') return 'Merak runtime needs attention';
    return 'Starting Merak runtime';
  }, [runtime]);

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
    if (response?.ok) setMessage(`Diagnostics exported to ${response.path}`);
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
      setMessage(formatApiError(error, 'Configuration could not be saved.'));
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
        <div className={styles.brand}>MERAK</div>
        <h1>{phase === 'configuring' ? 'Connect your model provider' : statusLabel}</h1>
        {phase !== 'configuring' && (
          <p>
            Merak is starting its local Windows runtime, workspace storage, and desktop bridge.
          </p>
        )}

        {phase === 'configuring' ? (
          <div className={styles.form}>
            <label>
              Provider
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
              API Key
              <input
                type="password"
                value={form.apiKey}
                onChange={(event) => setForm((prev) => ({ ...prev, apiKey: event.target.value }))}
                placeholder="Paste your provider key"
              />
            </label>
            <label>
              Model
              <input
                value={form.model}
                onChange={(event) => setForm((prev) => ({ ...prev, model: event.target.value }))}
                placeholder="gpt-4o"
              />
            </label>
            <label>
              API Base URL
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
              {saving ? 'Saving...' : 'Save and Start Workbench'}
            </button>
          </div>
        ) : (
          <div className={styles.statusGrid}>
            <span>Phase</span>
            <strong>{runtime?.phase ?? phase}</strong>
            <span>Runtime</span>
            <strong>{runtime?.apiBaseUrl ?? 'Selecting local port'}</strong>
            <span>Database</span>
            <strong>{runtime?.pgStatus ?? 'Checking'}</strong>
            <span>Logs</span>
            <strong>{runtime?.logPath ?? 'Preparing log file'}</strong>
          </div>
        )}

        {phase === 'failed' && (
          <>
            <div className={styles.actions}>
              <button type="button" onClick={handleRestart}>
                Restart Runtime
              </button>
              <button type="button" onClick={() => openDiagnosticsFolder()}>
                Open Logs
              </button>
              <button type="button" onClick={handleExportDiagnostics}>
                Export Diagnostics
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
