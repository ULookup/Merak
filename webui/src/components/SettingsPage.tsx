import { useEffect, useRef, useState } from 'react';
import { api, formatApiError } from '../api/client';
import type { LlmConfigFull, RuntimeMetadata } from '../api/types';
import {
  exportDiagnostics,
  getDesktopRuntimeLogs,
  getDesktopRuntimeStatus,
  isDesktopApp,
  openDiagnosticsFolder,
  restartDesktopRuntime,
  type DesktopRuntimeStatus,
} from '../desktop';
import styles from './SettingsPage.module.css';

const STYLE_OPTIONS = ['轻松', '严肃', '诗意', '简洁'] as const;

export default function SettingsPage() {
  const [config, setConfig] = useState<LlmConfigFull | null>(null);
  const [provider, setProvider] = useState('');
  const [model, setModel] = useState('');
  const [apiKey, setApiKey] = useState('');
  const [baseUrl, setBaseUrl] = useState('');
  const [genre, setGenre] = useState('');
  const [style, setStyle] = useState('');
  const [usageLogs, setUsageLogs] = useState(false);
  const [runtime, setRuntime] = useState<DesktopRuntimeStatus | null>(null);
  const [metadata, setMetadata] = useState<RuntimeMetadata | null>(null);
  const [logs, setLogs] = useState<string[]>([]);
  const [message, setMessage] = useState('');
  const [configError, setConfigError] = useState('');
  const [preferencesError, setPreferencesError] = useState('');
  const [preferencesLoaded, setPreferencesLoaded] = useState(false);
  const [metadataError, setMetadataError] = useState('');
  const [desktopError, setDesktopError] = useState('');
  const lifecycle = useRef(0);
  const desktopToken = useRef(0);
  const desktop = isDesktopApp();

  useEffect(() => {
    const life = ++lifecycle.current;
    let active = true;
    Promise.allSettled([api.getConfig(), api.getPreferences(), api.metadata()]).then(
      ([configResult, preferencesResult, metadataResult]) => {
        if (!active) return;
        if (configResult.status === 'fulfilled') {
          const next = configResult.value;
          setConfig(next);
          setProvider(next.provider);
          setModel(next.default_model);
          setBaseUrl(next.api_base_url);
        } else
          setConfigError(formatApiError(configResult.reason, 'Could not load model settings.'));
        if (preferencesResult.status === 'fulfilled') {
          const prefs = preferencesResult.value;
          setGenre(prefs.default_genre ?? '');
          setStyle(prefs.preferred_style ?? '');
          setUsageLogs(prefs.allow_usage_logs ?? false);
          setPreferencesLoaded(true);
        } else setPreferencesError(formatApiError(preferencesResult.reason, 'Request failed.'));
        if (metadataResult.status === 'fulfilled') setMetadata(metadataResult.value);
        else
          setMetadataError(
            `Runtime metadata unavailable: ${formatApiError(metadataResult.reason, 'Request failed.')}`,
          );
      },
    );
    if (desktop)
      getDesktopRuntimeStatus()
        .then((value) => {
          if (!active || life !== lifecycle.current) return;
          if (value) setRuntime(value);
          else setDesktopError('Desktop Runtime unavailable: Request failed.');
        })
        .catch(
          (error) =>
            active &&
            setDesktopError(
              `Desktop Runtime unavailable: ${formatApiError(error, 'Request failed.')}`,
            ),
        );
    return () => {
      active = false;
      lifecycle.current += 1;
      desktopToken.current += 1;
    };
  }, [desktop]);

  async function saveModel() {
    setMessage('');
    try {
      const result = await api.saveConfig({
        provider,
        default_model: model,
        api_base_url: baseUrl,
        api_key: apiKey || undefined,
      });
      setApiKey('');
      setMessage(
        result.restart_required
          ? 'Model settings saved. Runtime restart required.'
          : 'Model settings saved.',
      );
    } catch (error) {
      setMessage(formatApiError(error, 'Could not save model settings.'));
    }
  }

  async function savePreferences() {
    setMessage('');
    if (!preferencesLoaded) {
      setMessage('Preferences are unavailable and cannot be saved.');
      return;
    }
    if (!STYLE_OPTIONS.includes(style as (typeof STYLE_OPTIONS)[number])) {
      setMessage('Choose a supported preferred style before saving.');
      return;
    }
    try {
      await api.savePreferences({
        default_genre: genre,
        preferred_style: style,
        allow_usage_logs: usageLogs,
      });
      setMessage('Preferences saved.');
    } catch (error) {
      setMessage(formatApiError(error, 'Could not save preferences.'));
    }
  }

  async function runDesktopAction(
    operation: (isCurrent: () => boolean) => Promise<void>,
    fallback: string,
  ) {
    const life = lifecycle.current;
    const token = ++desktopToken.current;
    setDesktopError('');
    try {
      await operation(() => life === lifecycle.current && token === desktopToken.current);
    } catch (error) {
      if (life === lifecycle.current && token === desktopToken.current)
        setDesktopError(formatApiError(error, fallback));
    }
  }

  const refreshRuntime = () =>
    runDesktopAction(async (isCurrent) => {
      const result = await getDesktopRuntimeStatus();
      if (!result) throw new Error('Refresh runtime status failed.');
      if (isCurrent()) setRuntime(result);
    }, 'Refresh runtime status failed.');

  const restartRuntime = () =>
    runDesktopAction(async (isCurrent) => {
      const result = await restartDesktopRuntime();
      if (!result?.ok || !result.status)
        throw new Error(result?.error || 'Restart runtime failed.');
      if (isCurrent()) setRuntime(result.status);
    }, 'Restart runtime failed.');

  const openDiagnostics = () =>
    runDesktopAction(async () => {
      const result = await openDiagnosticsFolder();
      if (!result?.ok) throw new Error(result?.error || 'Open diagnostics failed.');
    }, 'Open diagnostics failed.');

  const exportDiagnosticReport = () =>
    runDesktopAction(async (isCurrent) => {
      const result = await exportDiagnostics();
      if (!result?.ok) throw new Error(result?.error || 'Export diagnostics failed.');
      if (isCurrent()) setMessage(`Diagnostics exported to ${result.path}`);
    }, 'Export diagnostics failed.');

  const viewRuntimeLogs = () =>
    runDesktopAction(async (isCurrent) => {
      const result = await getDesktopRuntimeLogs();
      if (!result || result.error) throw new Error(result?.error || 'View logs failed.');
      if (isCurrent()) setLogs(result.lines);
    }, 'View logs failed.');

  return (
    <main className={styles.page}>
      <header>
        <p className={styles.eyebrow}>Workspace</p>
        <h1>Settings</h1>
        <p>Configure only capabilities supported by the connected Merak Runtime.</p>
      </header>
      {message && (
        <div role="status" className={styles.message}>
          {message}
        </div>
      )}
      {preferencesError && (
        <div role="alert" className={styles.message}>
          Preferences unavailable: {preferencesError}
        </div>
      )}
      {metadataError && (
        <div role="alert" className={styles.message}>
          {metadataError}
        </div>
      )}
      {desktopError && (
        <div role="alert" className={styles.message}>
          {desktopError}
        </div>
      )}
      <div className={styles.grid}>
        <section className={styles.card}>
          <h2>Model provider</h2>
          {config ? (
            <>
              <label>
                Provider
                <select value={provider} onChange={(event) => setProvider(event.target.value)}>
                  <option value="anthropic">Anthropic</option>
                  <option value="openai">OpenAI</option>
                  <option value="custom">Custom</option>
                </select>
              </label>
              <label>
                Default model
                <input value={model} onChange={(event) => setModel(event.target.value)} />
              </label>
              <label>
                API base URL
                <input
                  aria-label="API base URL"
                  value={baseUrl}
                  onChange={(event) => setBaseUrl(event.target.value)}
                />
              </label>
              <label>
                API key
                <input
                  aria-label="API key"
                  type="password"
                  value={apiKey}
                  placeholder={config.api_key_masked || 'Not configured'}
                  onChange={(event) => setApiKey(event.target.value)}
                  autoComplete="new-password"
                />
              </label>
              <p className={styles.hint}>Stored credentials are always returned masked.</p>
              <div className={styles.actions}>
                <button
                  type="button"
                  onClick={() =>
                    api
                      .testConfig()
                      .then(() => setMessage('Connection succeeded.'))
                      .catch((error) => setMessage(formatApiError(error, 'Connection failed.')))
                  }
                >
                  Test connection
                </button>
                <button type="button" onClick={saveModel}>
                  Save model settings
                </button>
              </div>
            </>
          ) : configError ? (
            <p role="alert">{configError}</p>
          ) : (
            <p>Loading model settings...</p>
          )}
        </section>

        <section className={styles.card}>
          <h2>Writing preferences</h2>
          {preferencesLoaded ? (
            <>
              <label>
                Default genre
                <input value={genre} onChange={(event) => setGenre(event.target.value)} />
              </label>
              <label>
                Preferred style
                <select
                  aria-label="Preferred style"
                  value={style}
                  onChange={(event) => setStyle(event.target.value)}
                >
                  {STYLE_OPTIONS.map((option) => (
                    <option key={option} value={option}>
                      {option}
                    </option>
                  ))}
                </select>
              </label>
              <label className={styles.check}>
                <input
                  type="checkbox"
                  checked={usageLogs}
                  onChange={(event) => setUsageLogs(event.target.checked)}
                />
                Allow usage logs
              </label>
              <div className={styles.actions}>
                <button
                  type="button"
                  disabled={!STYLE_OPTIONS.includes(style as (typeof STYLE_OPTIONS)[number])}
                  onClick={savePreferences}
                >
                  Save preferences
                </button>
              </div>
            </>
          ) : (
            <p>{preferencesError ? 'Preferences are unavailable.' : 'Loading preferences...'}</p>
          )}
        </section>

        <section className={styles.card}>
          <h2>Runtime and diagnostics</h2>
          {desktop ? (
            <>
              <dl>
                <dt>Health</dt>
                <dd>{runtime?.phase ?? 'Unavailable'}</dd>
                <dt>API</dt>
                <dd>{runtime?.apiBaseUrl ?? 'Unavailable'}</dd>
                {metadata ? (
                  <>
                    <dt>Permission mode</dt>
                    <dd>{metadata.permission_mode}</dd>
                  </>
                ) : null}
                {runtime?.pgStatus ? (
                  <>
                    <dt>PostgreSQL</dt>
                    <dd>{runtime.pgStatus}</dd>
                  </>
                ) : null}
              </dl>
              {runtime?.error && <p role="alert">{runtime.error}</p>}
              <div className={styles.actions}>
                <button type="button" onClick={refreshRuntime}>
                  Refresh
                </button>
                <button type="button" onClick={restartRuntime}>
                  Restart Runtime
                </button>
                <button type="button" onClick={openDiagnostics}>
                  Open diagnostics
                </button>
                <button type="button" onClick={exportDiagnosticReport}>
                  Export diagnostics
                </button>
                <button type="button" onClick={viewRuntimeLogs}>
                  View logs
                </button>
              </div>
              {logs.length > 0 && <pre className={styles.logs}>{logs.join('\n')}</pre>}
            </>
          ) : (
            <p role="status">Desktop diagnostics are unavailable in the web app.</p>
          )}
        </section>
      </div>
    </main>
  );
}
