import { useEffect, useState } from 'react';
import { api, formatApiError } from '../../api/client';
import {
  exportDiagnostics,
  getDesktopRuntimeStatus,
  isDesktopApp,
  openDiagnosticsFolder,
  restartDesktopRuntime,
  type DesktopRuntimeStatus,
} from '../../desktop';
import { useI18n } from '../../i18n';
import styles from './SettingsPanel.module.css';

interface ConfigState {
  provider: string;
  api_key_masked: string;
  api_base_url: string;
  default_model: string;
  max_output_tokens: number;
}

export default function SettingsPanel() {
  const { t } = useI18n();
  const [config, setConfig] = useState<ConfigState | null>(null);
  const [apiKey, setApiKey] = useState('');
  const [provider, setProvider] = useState('anthropic');
  const [model, setModel] = useState('');
  const [baseUrl, setBaseUrl] = useState('');
  const [maxOutputTokens, setMaxOutputTokens] = useState(4096);
  const [showKey, setShowKey] = useState(false);
  const [saving, setSaving] = useState(false);
  const [testing, setTesting] = useState(false);
  const [status, setStatus] = useState<'idle' | 'saved' | 'test_ok' | 'test_fail' | 'error'>(
    'idle',
  );
  const [errorMsg, setErrorMsg] = useState('');
  const [runtime, setRuntime] = useState<DesktopRuntimeStatus | null>(null);
  const [diagnosticMsg, setDiagnosticMsg] = useState('');

  const loadConfig = async () => {
    const data = await api.getConfig();
    setConfig(data);
    setProvider(data.provider || 'anthropic');
    setModel(data.default_model || '');
    setBaseUrl(data.api_base_url || '');
    setMaxOutputTokens(data.max_output_tokens || 4096);
  };

  useEffect(() => {
    loadConfig().catch((error) => {
      setStatus('error');
      setErrorMsg(formatApiError(error, t('settings.saveFail')));
    });
  }, [t]);

  useEffect(() => {
    if (!isDesktopApp()) return;
    getDesktopRuntimeStatus().then(setRuntime);
  }, []);

  const handleSave = async () => {
    setSaving(true);
    setStatus('idle');
    setErrorMsg('');
    try {
      await api.saveConfig({
        provider,
        api_key: apiKey || undefined,
        default_model: model || undefined,
        api_base_url: baseUrl || undefined,
        max_output_tokens: maxOutputTokens,
      });
      await loadConfig();
      setStatus('saved');
      setApiKey('');
    } catch (error) {
      setStatus('error');
      setErrorMsg(formatApiError(error, t('settings.saveFail')));
    } finally {
      setSaving(false);
    }
  };

  const handleTest = async () => {
    setTesting(true);
    setStatus('idle');
    setErrorMsg('');
    try {
      await api.testConfig();
      setStatus('test_ok');
    } catch (error) {
      setStatus('test_fail');
      setErrorMsg(formatApiError(error, t('settings.testFail')));
    } finally {
      setTesting(false);
    }
  };

  const refreshRuntime = async () => {
    setRuntime(await getDesktopRuntimeStatus());
  };

  const handleRestartRuntime = async () => {
    setDiagnosticMsg('');
    const response = await restartDesktopRuntime();
    setRuntime(response?.status ?? null);
    setDiagnosticMsg(response?.ok ? t('settings.restartOk') : t('settings.restartFail'));
    if (response?.ok) {
      loadConfig().catch(() => {});
    }
  };

  const handleExportDiagnostics = async () => {
    const response = await exportDiagnostics();
    if (response?.ok) setDiagnosticMsg(`${t('settings.exported')} ${response.path}`);
    else if (response) setDiagnosticMsg(response.path);
  };

  if (!config) {
    return (
      <div className={styles.panel}>
        <h3 className={styles.heading}>{t('settings.title')}</h3>
        {status === 'error' ? <div className={styles.error}>{errorMsg}</div> : t('settings.loading')}
      </div>
    );
  }

  return (
    <div className={styles.panel}>
      <h3 className={styles.heading}>{t('settings.title')}</h3>

      <label className={styles.label}>
        {t('settings.service')}
        <select
          value={provider}
          onChange={(event) => setProvider(event.target.value)}
          className={styles.input}
        >
          <option value="anthropic">Anthropic</option>
          <option value="openai">OpenAI</option>
          <option value="custom">Custom</option>
        </select>
      </label>

      <label className={styles.label}>
        {t('settings.accessKey')}
        <div className={styles.keyRow}>
          <input
            type={showKey ? 'text' : 'password'}
            value={apiKey}
            onChange={(event) => setApiKey(event.target.value)}
            placeholder={config.api_key_masked || 'sk-...'}
            className={styles.input}
          />
          <button type="button" onClick={() => setShowKey(!showKey)} className={styles.toggleBtn}>
            {showKey ? t('settings.hide') : t('settings.show')}
          </button>
        </div>
      </label>

      <label className={styles.label}>
        {t('settings.assistant')}
        <input
          type="text"
          value={model}
          onChange={(event) => setModel(event.target.value)}
          placeholder={config.default_model || 'claude-sonnet-4-6'}
          className={styles.input}
        />
      </label>

      <label className={styles.label}>
        {t('settings.advancedAddress')}
        <input
          type="text"
          value={baseUrl}
          onChange={(event) => setBaseUrl(event.target.value)}
          placeholder={config.api_base_url || 'https://api.anthropic.com'}
          className={styles.input}
        />
      </label>

      <label className={styles.label}>
        最大输出 Token
        <input
          type="number"
          min={256}
          step={256}
          value={maxOutputTokens}
          onChange={(event) => setMaxOutputTokens(Math.max(256, Number(event.target.value) || 256))}
          placeholder={String(config.max_output_tokens || 4096)}
          className={styles.input}
        />
      </label>

      <div className={styles.actions}>
        <button onClick={handleTest} disabled={testing} className={styles.btn}>
          {testing ? t('settings.testing') : t('settings.test')}
        </button>
        <button
          onClick={handleSave}
          disabled={saving}
          className={`${styles.btn} ${styles.primary}`}
        >
          {saving ? t('settings.saving') : t('settings.save')}
        </button>
      </div>

      {status === 'saved' && <div className={styles.ok}>{t('settings.saved')}</div>}
      {status === 'test_ok' && <div className={styles.ok}>{t('settings.testOk')}</div>}
      {status === 'test_fail' && <div className={styles.error}>{errorMsg}</div>}
      {status === 'error' && <div className={styles.error}>{errorMsg}</div>}

      {isDesktopApp() && (
        <section className={styles.runtimePanel}>
          <h3 className={styles.heading}>{t('settings.desktopStatus')}</h3>
          <div className={styles.runtimeGrid}>
            <span>{t('settings.phase')}</span>
            <strong>{runtime?.phase ?? t('settings.unknown')}</strong>
            <span>{t('settings.localAddress')}</span>
            <strong>{runtime?.apiBaseUrl ?? t('settings.notReady')}</strong>
            <span>{t('settings.process')}</span>
            <strong>{runtime?.pid ? `PID ${runtime.pid}` : t('settings.notRunning')}</strong>
            <span>{t('settings.localLibrary')}</span>
            <strong>{runtime?.pgStatus ?? t('settings.unknown')}</strong>
            <span>{t('settings.configFile')}</span>
            <strong>{runtime?.configPath ?? t('settings.unknown')}</strong>
            <span>{t('settings.reportFile')}</span>
            <strong>{runtime?.logPath ?? t('settings.unknown')}</strong>
          </div>
          {runtime?.error && <div className={styles.error}>{runtime.error}</div>}
          <div className={styles.actions}>
            <button type="button" onClick={refreshRuntime} className={styles.btn}>
              {t('settings.refresh')}
            </button>
            <button type="button" onClick={handleRestartRuntime} className={styles.btn}>
              {t('settings.restart')}
            </button>
            <button type="button" onClick={() => openDiagnosticsFolder()} className={styles.btn}>
              {t('settings.openReports')}
            </button>
            <button type="button" onClick={handleExportDiagnostics} className={styles.btn}>
              {t('settings.exportReport')}
            </button>
          </div>
          {diagnosticMsg && <div className={styles.ok}>{diagnosticMsg}</div>}
        </section>
      )}
    </div>
  );
}
