import { useEffect, useState } from 'react';
import { api, formatApiError } from '../../api/client';
import styles from './SettingsPanel.module.css';

interface ConfigState {
  provider: string;
  api_key_masked: string;
  api_base_url: string;
  default_model: string;
  max_output_tokens: number;
}

export default function SettingsPanel() {
  const [config, setConfig] = useState<ConfigState | null>(null);
  const [apiKey, setApiKey] = useState('');
  const [provider, setProvider] = useState('anthropic');
  const [model, setModel] = useState('');
  const [baseUrl, setBaseUrl] = useState('');
  const [showKey, setShowKey] = useState(false);
  const [saving, setSaving] = useState(false);
  const [testing, setTesting] = useState(false);
  const [status, setStatus] = useState<'idle' | 'saved' | 'test_ok' | 'test_fail' | 'error'>('idle');
  const [errorMsg, setErrorMsg] = useState('');

  useEffect(() => {
    api.getConfig().then((data) => {
      setConfig(data);
      setProvider(data.provider || 'anthropic');
      setModel(data.default_model || '');
      setBaseUrl(data.api_base_url || '');
    }).catch(() => {});
  }, []);

  const handleSave = async () => {
    setSaving(true);
    setStatus('idle');
    try {
      await api.saveConfig({
        provider,
        api_key: apiKey || undefined,
        default_model: model || undefined,
        api_base_url: baseUrl || undefined,
      });
      setStatus('saved');
      setApiKey('');
    } catch (e) {
      setStatus('error');
      setErrorMsg(formatApiError(e, 'Save failed'));
    } finally {
      setSaving(false);
    }
  };

  const handleTest = async () => {
    setTesting(true);
    setStatus('idle');
    try {
      await api.testConfig();
      setStatus('test_ok');
    } catch (e) {
      setStatus('test_fail');
      setErrorMsg(formatApiError(e, 'LLM connection test failed — check your API key and network.'));
    } finally {
      setTesting(false);
    }
  };

  if (!config) return <div className={styles.panel}>Loading config...</div>;

  return (
    <div className={styles.panel}>
      <h3 className={styles.heading}>Model Configuration</h3>

      <label className={styles.label}>
        Provider
        <select value={provider} onChange={(e) => setProvider(e.target.value)} className={styles.input}>
          <option value="anthropic">Anthropic</option>
          <option value="openai">OpenAI</option>
          <option value="custom">Custom</option>
        </select>
      </label>

      <label className={styles.label}>
        API Key
        <div className={styles.keyRow}>
          <input
            type={showKey ? 'text' : 'password'}
            value={apiKey}
            onChange={(e) => setApiKey(e.target.value)}
            placeholder={config.api_key_masked || 'sk-...'}
            className={styles.input}
          />
          <button type="button" onClick={() => setShowKey(!showKey)} className={styles.toggleBtn}>
            {showKey ? 'Hide' : 'Show'}
          </button>
        </div>
      </label>

      <label className={styles.label}>
        Model
        <input
          type="text"
          value={model}
          onChange={(e) => setModel(e.target.value)}
          placeholder={config.default_model || 'claude-sonnet-4-6'}
          className={styles.input}
        />
      </label>

      <label className={styles.label}>
        API Base URL (optional)
        <input
          type="text"
          value={baseUrl}
          onChange={(e) => setBaseUrl(e.target.value)}
          placeholder={config.api_base_url || 'https://api.anthropic.com'}
          className={styles.input}
        />
      </label>

      <div className={styles.actions}>
        <button onClick={handleTest} disabled={testing} className={styles.btn}>
          {testing ? 'Testing...' : 'Test Connection'}
        </button>
        <button onClick={handleSave} disabled={saving} className={`${styles.btn} ${styles.primary}`}>
          {saving ? 'Saving...' : 'Save Config'}
        </button>
      </div>

      {status === 'saved' && (
        <div className={styles.ok}>Configuration saved. Restart server to apply changes.</div>
      )}
      {status === 'test_ok' && (
        <div className={styles.ok}>LLM connection test passed.</div>
      )}
      {status === 'test_fail' && <div className={styles.error}>{errorMsg}</div>}
      {status === 'error' && <div className={styles.error}>{errorMsg}</div>}
    </div>
  );
}
