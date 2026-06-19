import { useEffect, useState } from 'react';
import { ArrowLeft } from 'lucide-react';
import { api, formatApiError } from '../api/client';
import { useAppState } from '../AppState';
import {
  exportDiagnostics,
  getDesktopRuntimeStatus,
  isDesktopApp,
  openDiagnosticsFolder,
  restartDesktopRuntime,
  type DesktopRuntimeStatus,
} from '../desktop';
import styles from './SettingsPage.module.css';

interface ConfigState {
  provider: string;
  api_key_masked: string;
  api_base_url: string;
  default_model: string;
  max_output_tokens: number;
}

const GENRE_OPTIONS = [
  { value: '无偏好', label: '无偏好' },
  { value: '修仙', label: '修仙' },
  { value: '都市', label: '都市' },
  { value: '悬疑', label: '悬疑' },
  { value: '科幻', label: '科幻' },
  { value: '言情', label: '言情' },
  { value: '历史', label: '历史' },
];

const STYLE_OPTIONS = [
  { value: '轻松', label: '轻松' },
  { value: '严肃', label: '严肃' },
  { value: '诗意', label: '诗意' },
  { value: '简洁', label: '简洁' },
];

export default function SettingsPage() {
  const { dispatch } = useAppState();

  // LLM config
  const [config, setConfig] = useState<ConfigState | null>(null);
  const [apiKey, setApiKey] = useState('');
  const [provider, setProvider] = useState('anthropic');
  const [model, setModel] = useState('');
  const [baseUrl, setBaseUrl] = useState('');
  const [maxOutputTokens, setMaxOutputTokens] = useState(4096);
  const [showKey, setShowKey] = useState(false);
  const [saving, setSaving] = useState(false);
  const [testing, setTesting] = useState(false);
  const [llmStatus, setLlmStatus] = useState<'idle' | 'saved' | 'test_ok' | 'test_fail' | 'error'>('idle');
  const [llmError, setLlmError] = useState('');

  // User preferences
  const [genre, setGenre] = useState('无偏好');
  const [style, setStyle] = useState('轻松');
  const [allowUsageLogs, setAllowUsageLogs] = useState(true);
  const [prefsLoaded, setPrefsLoaded] = useState(false);
  const [prefSaving, setPrefSaving] = useState(false);
  const [prefError, setPrefError] = useState<string | null>(null);
  const [prefSaved, setPrefSaved] = useState(false);

  // Desktop runtime
  const [runtime, setRuntime] = useState<DesktopRuntimeStatus | null>(null);
  const [diagnosticMsg, setDiagnosticMsg] = useState('');

  useEffect(() => {
    api.getConfig()
      .then((data) => {
        setConfig(data);
        setProvider(data.provider || 'anthropic');
        setModel(data.default_model || '');
        setBaseUrl(data.api_base_url || '');
        setMaxOutputTokens(data.max_output_tokens || 4096);
      })
      .catch((error) => {
        setLlmStatus('error');
        setLlmError(formatApiError(error, '加载设置失败'));
      });
  }, []);

  useEffect(() => {
    api.getPreferences()
      .then((prefs) => {
        if (prefs.default_genre) setGenre(prefs.default_genre);
        if (prefs.preferred_style) setStyle(prefs.preferred_style);
        if (prefs.allow_usage_logs !== undefined) setAllowUsageLogs(prefs.allow_usage_logs);
        setPrefsLoaded(true);
      })
      .catch(() => setPrefsLoaded(true));
  }, []);

  useEffect(() => {
    if (!isDesktopApp()) return;
    getDesktopRuntimeStatus().then(setRuntime);
  }, []);

  const handleSaveLlm = async () => {
    setSaving(true);
    setLlmStatus('idle');
    setLlmError('');
    try {
      await api.saveConfig({
        provider,
        api_key: apiKey || undefined,
        default_model: model || undefined,
        api_base_url: baseUrl || undefined,
        max_output_tokens: maxOutputTokens,
      });
      const data = await api.getConfig();
      setConfig(data);
      setLlmStatus('saved');
      setApiKey('');
    } catch (error) {
      setLlmStatus('error');
      setLlmError(formatApiError(error, '保存设置失败'));
    } finally {
      setSaving(false);
    }
  };

  const handleTest = async () => {
    setTesting(true);
    setLlmStatus('idle');
    setLlmError('');
    try {
      await api.testConfig();
      setLlmStatus('test_ok');
    } catch (error) {
      setLlmStatus('test_fail');
      setLlmError(formatApiError(error, '连接测试失败'));
    } finally {
      setTesting(false);
    }
  };

  const handleSavePrefs = async () => {
    setPrefSaving(true);
    setPrefError(null);
    setPrefSaved(false);
    try {
      await api.savePreferences({
        default_genre: genre,
        preferred_style: style,
        allow_usage_logs: allowUsageLogs,
      });
      dispatch({ type: 'SET_USER_PREFERENCES', prefs: { default_genre: genre, preferred_style: style } });
      setPrefSaved(true);
      setTimeout(() => setPrefSaved(false), 2000);
    } catch (error) {
      setPrefError(formatApiError(error, '保存偏好设置失败'));
    } finally {
      setPrefSaving(false);
    }
  };

  const refreshRuntime = async () => {
    setRuntime(await getDesktopRuntimeStatus());
  };

  const handleRestartRuntime = async () => {
    setDiagnosticMsg('');
    const response = await restartDesktopRuntime();
    setRuntime(response?.status ?? null);
    setDiagnosticMsg(response?.ok ? 'Merak 已重新打开。' : 'Merak 没能完成重新打开。');
    if (response?.ok) {
      api.getConfig().then((data) => {
        setConfig(data);
        setProvider(data.provider || 'anthropic');
        setModel(data.default_model || '');
      }).catch(() => {});
    }
  };

  const handleExportDiagnostics = async () => {
    const response = await exportDiagnostics();
    if (response?.ok) setDiagnosticMsg(`故障报告已导出：${response.path}`);
    else if (response) setDiagnosticMsg(response.path);
  };

  return (
    <div className={styles.page}>
      <div className={styles.header}>
        <button
          className={styles.backBtn}
          onClick={() => dispatch({ type: 'SET_PAGE', page: 'overview' })}
        >
          <ArrowLeft size={15} aria-hidden="true" strokeWidth={2.3} />
          返回工作台
        </button>
        <h1 className={styles.headerTitle}>设置</h1>
      </div>

      <div className={styles.body}>
        {/* LLM Configuration */}
        <div className={styles.section}>
          <h2 className={styles.sectionTitle}>大模型配置</h2>

          {!config ? (
            llmStatus === 'error' ? <div className={styles.error}>{llmError}</div> : <p className={styles.hint}>正在加载配置...</p>
          ) : (
            <>
              <div className={styles.field}>
                <label className={styles.label}>
                  服务类型
                  <select value={provider} onChange={(e) => setProvider(e.target.value)} className={styles.select}>
                    <option value="anthropic">Anthropic</option>
                    <option value="openai">OpenAI</option>
                    <option value="custom">Custom</option>
                  </select>
                </label>
              </div>

              <div className={styles.field}>
                <label className={styles.label}>
                  访问密钥
                  <div className={styles.keyRow}>
                    <input
                      type={showKey ? 'text' : 'password'}
                      value={apiKey}
                      onChange={(e) => setApiKey(e.target.value)}
                      placeholder={config.api_key_masked || 'sk-...'}
                      className={styles.input}
                    />
                    <button type="button" onClick={() => setShowKey(!showKey)} className={styles.toggleBtn}>
                      {showKey ? '隐藏' : '显示'}
                    </button>
                  </div>
                </label>
              </div>

              <div className={styles.field}>
                <label className={styles.label}>
                  创作助手模型
                  <input
                    type="text"
                    value={model}
                    onChange={(e) => setModel(e.target.value)}
                    placeholder={config.default_model || 'claude-sonnet-4-6'}
                    className={styles.input}
                  />
                </label>
              </div>

              <div className={styles.field}>
                <label className={styles.label}>
                  高级连接地址
                  <input
                    type="text"
                    value={baseUrl}
                    onChange={(e) => setBaseUrl(e.target.value)}
                    placeholder={config.api_base_url || 'https://api.anthropic.com'}
                    className={styles.input}
                  />
                </label>
              </div>

              <div className={styles.field}>
                <label className={styles.label}>
                  最大输出 Token
                  <input
                    type="number"
                    min={256}
                    step={256}
                    value={maxOutputTokens}
                    onChange={(e) => setMaxOutputTokens(Math.max(256, Number(e.target.value) || 256))}
                    placeholder={String(config.max_output_tokens || 4096)}
                    className={styles.input}
                  />
                </label>
              </div>

              <div className={styles.actions}>
                <button onClick={handleTest} disabled={testing} className={styles.btn}>
                  {testing ? '测试中...' : '测试连接'}
                </button>
                <button onClick={handleSaveLlm} disabled={saving} className={styles.btnPrimary}>
                  {saving ? '保存中...' : '保存配置'}
                </button>
              </div>

              {llmStatus === 'saved' && <div className={styles.ok}>配置已保存，重启后生效。</div>}
              {llmStatus === 'test_ok' && <div className={styles.ok}>连接测试通过。</div>}
              {llmStatus === 'test_fail' && <div className={styles.error}>{llmError}</div>}
              {llmStatus === 'error' && <div className={styles.error}>{llmError}</div>}
            </>
          )}
        </div>

        {/* User Preferences */}
        <div className={styles.section}>
          <h2 className={styles.sectionTitle}>创作偏好</h2>

          {!prefsLoaded ? (
            <p className={styles.hint}>正在加载偏好...</p>
          ) : (
            <>
              <div className={styles.field}>
                <label className={styles.label}>
                  默认题材
                  <select value={genre} onChange={(e) => { setGenre(e.target.value); setPrefSaved(false); }} className={styles.select} disabled={prefSaving}>
                    {GENRE_OPTIONS.map((g) => (
                      <option key={g.value} value={g.value}>{g.label}</option>
                    ))}
                  </select>
                </label>
              </div>

              <div className={styles.field}>
                <label className={styles.label}>
                  偏好风格
                  <select value={style} onChange={(e) => { setStyle(e.target.value); setPrefSaved(false); }} className={styles.select} disabled={prefSaving}>
                    {STYLE_OPTIONS.map((s) => (
                      <option key={s.value} value={s.value}>{s.label}</option>
                    ))}
                  </select>
                </label>
              </div>

              <div className={styles.field}>
                <label className={styles.label}>使用情况日志</label>
                <div className={styles.toggle}>
                  <button
                    type="button"
                    className={styles.toggleSwitch}
                    role="switch"
                    aria-checked={allowUsageLogs}
                    aria-label="切换使用情况日志"
                    onClick={() => { setAllowUsageLogs((prev) => !prev); setPrefSaved(false); }}
                    disabled={prefSaving}
                  >
                    <span className={styles.toggleKnob} />
                  </button>
                  <span className={styles.toggleLabel}>{allowUsageLogs ? '已开启' : '已关闭'}</span>
                </div>
              </div>

              <div className={styles.actions}>
                <button onClick={handleSavePrefs} disabled={prefSaving} className={styles.btnPrimary}>
                  {prefSaving ? '保存中...' : '保存偏好'}
                </button>
              </div>

              {prefError && <div className={styles.error}>{prefError}</div>}
              {prefSaved && <div className={styles.ok}>偏好已保存</div>}
            </>
          )}
        </div>

        {/* Desktop Runtime Status */}
        {isDesktopApp() && (
          <div className={styles.section}>
            <h2 className={styles.sectionTitle}>本地桌面状态</h2>
            <div className={styles.runtimePanel}>
              <div className={styles.runtimeGrid}>
                <span>当前步骤</span>
                <strong>{runtime?.phase ?? '未知'}</strong>
                <span>本机连接</span>
                <strong>{runtime?.apiBaseUrl ?? '未准备好'}</strong>
                <span>本地进程</span>
                <strong>{runtime?.pid ? `PID ${runtime.pid}` : '未运行'}</strong>
                <span>本地资料库</span>
                <strong>{runtime?.pgStatus ?? '未知'}</strong>
                <span>设置文件</span>
                <strong>{runtime?.configPath ?? '未知'}</strong>
                <span>报告文件</span>
                <strong>{runtime?.logPath ?? '未知'}</strong>
              </div>
              {runtime?.error && <div className={styles.error}>{runtime.error}</div>}
              <div className={styles.actions}>
                <button type="button" onClick={refreshRuntime} className={styles.btn}>刷新</button>
                <button type="button" onClick={handleRestartRuntime} className={styles.btn}>重新打开 Merak</button>
                <button type="button" onClick={() => openDiagnosticsFolder()} className={styles.btn}>打开报告文件夹</button>
                <button type="button" onClick={handleExportDiagnostics} className={styles.btn}>导出故障报告</button>
              </div>
              {diagnosticMsg && <div className={styles.ok}>{diagnosticMsg}</div>}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
