import { useState } from 'react';
import { api, formatApiError } from '../api/client';
import styles from './SetupWizard.module.css';

interface SetupWizardProps {
  onComplete: () => void;
}

type Step = 'provider' | 'key' | 'test';

const PROVIDERS = [
  { value: 'openai', label: 'OpenAI' },
  { value: 'anthropic', label: 'Anthropic' },
  { value: 'deepseek', label: 'DeepSeek' },
  { value: 'custom', label: '自定义' },
];

const CONTEXT_MEMORY_OPTIONS = [
  { value: 'short', label: '短 (最近几轮)' },
  { value: 'medium', label: '中 (最近几十轮)' },
  { value: 'long', label: '长 (尽可能多)' },
] as const;

export default function SetupWizard({ onComplete }: SetupWizardProps) {
  const [step, setStep] = useState<Step>('provider');
  const [provider, setProvider] = useState('openai');
  const [apiKey, setApiKey] = useState('');
  const [model, setModel] = useState('');
  const [apiBaseUrl, setApiBaseUrl] = useState('');
  const [temperature, setTemperature] = useState(0.7);
  const [maxTokens, setMaxTokens] = useState(4096);
  const [contextMemory, setContextMemory] = useState<'short' | 'medium' | 'long'>('medium');
  const [writerModel, setWriterModel] = useState('');
  const [showAdvanced, setShowAdvanced] = useState(false);
  const [saving, setSaving] = useState(false);
  const [testing, setTesting] = useState(false);
  const [testResult, setTestResult] = useState<{ success: boolean; message: string } | null>(null);
  const [error, setError] = useState<string | null>(null);

  function resetErrors() {
    setError(null);
    setTestResult(null);
  }

  function goToProvider() {
    resetErrors();
    setStep('provider');
  }

  function goToKey() {
    resetErrors();
    if (!provider) {
      setError('请选择一个 LLM 提供商。');
      return;
    }
    setStep('key');
  }

  async function handleSaveAndTest() {
    if (!apiKey.trim()) {
      setError('请输入 API 密钥。');
      return;
    }
    if (!model.trim()) {
      setError('请输入模型名称。');
      return;
    }

    setSaving(true);
    setError(null);

    try {
      await api.saveConfig({
        provider,
        api_key: apiKey.trim(),
        api_base_url: provider === 'custom' ? apiBaseUrl.trim() || undefined : undefined,
        default_model: model.trim(),
        max_output_tokens: maxTokens,
        temperature,
        context_memory_length: contextMemory,
        ...(writerModel.trim() ? { writer_model: writerModel.trim() } : {}),
      });
      setStep('test');
    } catch (e: unknown) {
      setError(formatApiError(e, '保存配置失败，请稍后重试。'));
    } finally {
      setSaving(false);
    }
  }

  async function handleTest() {
    setTesting(true);
    setTestResult(null);
    setError(null);

    try {
      const result = await api.testConfig();
      setTestResult({
        success: true,
        message: result.message || '连接测试成功！配置已生效。',
      });
    } catch (e: unknown) {
      const msg = formatApiError(e, '连接测试失败，请检查配置。');
      setTestResult({ success: false, message: msg });
    } finally {
      setTesting(false);
    }
  }

  function goBackToKey() {
    resetErrors();
    setStep('key');
  }

  function renderProviderStep() {
    return (
      <>
        <h2 className={styles.title}>初始化设置</h2>
        <p className={styles.subtitle}>
          欢迎使用 Merak！请选择一个 LLM 提供商以开始创作。
        </p>

        <div className={styles.field}>
          <label className={styles.label}>LLM 提供商</label>
          <select
            className={styles.select}
            value={provider}
            onChange={(e) => {
              setProvider(e.target.value);
              resetErrors();
            }}
          >
            {PROVIDERS.map((p) => (
              <option key={p.value} value={p.value}>
                {p.label}
              </option>
            ))}
          </select>
        </div>

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.btnPrimary} onClick={goToKey}>
            下一步
          </button>
        </div>
      </>
    );
  }

  function renderKeyStep() {
    const providerLabel = PROVIDERS.find((p) => p.value === provider)?.label || provider;

    return (
      <>
        <h2 className={styles.title}>配置 {providerLabel}</h2>
        <p className={styles.subtitle}>
          请输入您的 API 密钥和模型信息。
        </p>

        {provider === 'custom' && (
          <div className={styles.field}>
            <label className={styles.label}>自定义 API 地址</label>
            <input
              className={styles.input}
              type="text"
              value={apiBaseUrl}
              onChange={(e) => setApiBaseUrl(e.target.value)}
              placeholder="例如：https://api.example.com/v1"
              disabled={saving}
            />
          </div>
        )}

        <div className={styles.field}>
          <label className={styles.label}>API 密钥</label>
          <input
            className={styles.input}
            type="password"
            value={apiKey}
            onChange={(e) => setApiKey(e.target.value)}
            placeholder="请输入您的 API 密钥"
            disabled={saving}
            autoFocus
          />
        </div>

        <div className={styles.field}>
          <label className={styles.label}>模型名称</label>
          <input
            className={styles.input}
            type="text"
            value={model}
            onChange={(e) => setModel(e.target.value)}
            placeholder="例如：gpt-4o, claude-sonnet-4-20250514"
            disabled={saving}
          />
        </div>

        <button
          className={styles.advancedToggle}
          onClick={() => setShowAdvanced((prev) => !prev)}
          type="button"
        >
          {showAdvanced ? '收起高级设置' : '展开高级设置'}
        </button>

        {showAdvanced && (
          <div className={styles.advancedFields}>
            <div className={styles.field}>
              <label className={styles.label}>
                温度 (Temperature): {temperature.toFixed(1)}
              </label>
              <div className={styles.rangeRow}>
                <span className={styles.rangeValue}>0</span>
                <input
                  className={styles.rangeSlider}
                  type="range"
                  min="0"
                  max="2"
                  step="0.1"
                  value={temperature}
                  onChange={(e) => setTemperature(parseFloat(e.target.value))}
                  disabled={saving}
                />
                <span className={styles.rangeValue}>2</span>
              </div>
            </div>

            <div className={styles.field}>
              <label className={styles.label}>最大 Token 数</label>
              <input
                className={styles.input}
                type="number"
                value={maxTokens}
                onChange={(e) => setMaxTokens(Number(e.target.value))}
                min={256}
                max={131072}
                disabled={saving}
              />
            </div>

            <div className={styles.field}>
              <label className={styles.label}>上下文记忆长度</label>
              <select
                className={styles.select}
                value={contextMemory}
                onChange={(e) =>
                  setContextMemory(e.target.value as 'short' | 'medium' | 'long')
                }
                disabled={saving}
              >
                {CONTEXT_MEMORY_OPTIONS.map((opt) => (
                  <option key={opt.value} value={opt.value}>
                    {opt.label}
                  </option>
                ))}
              </select>
            </div>

            <div className={styles.field}>
              <label className={styles.label}>Writer 模型名称</label>
              <input
                className={styles.input}
                type="text"
                value={writerModel}
                onChange={(e) => setWriterModel(e.target.value)}
                placeholder="默认与主模型相同"
                disabled={saving}
              />
            </div>
          </div>
        )}

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.btnSecondary} onClick={goToProvider} disabled={saving}>
            返回
          </button>
          <button className={styles.btnPrimary} onClick={handleSaveAndTest} disabled={saving}>
            {saving ? '保存中...' : '保存并测试'}
          </button>
        </div>
      </>
    );
  }

  function renderTestStep() {
    return (
      <>
        <h2 className={styles.title}>测试连接</h2>
        <p className={styles.subtitle}>
          点击下方按钮测试与 {PROVIDERS.find((p) => p.value === provider)?.label || provider} 的连接。
        </p>

        {testResult && (
          <div
            className={`${styles.testResult} ${
              testResult.success ? styles.testResultSuccess : styles.testResultFailed
            }`}
          >
            {testResult.message}
          </div>
        )}

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          {testResult && !testResult.success ? (
            <>
              <button className={styles.btnSecondary} onClick={goBackToKey} disabled={testing}>
                返回修改
              </button>
              <button className={styles.btnPrimary} onClick={handleTest} disabled={testing}>
                {testing ? '测试中...' : '重新测试'}
              </button>
            </>
          ) : testResult && testResult.success ? (
            <button className={styles.btnPrimary} onClick={onComplete}>
              开始创作
            </button>
          ) : (
            <>
              <button className={styles.btnSecondary} onClick={goBackToKey} disabled={testing}>
                返回修改
              </button>
              <button className={styles.btnPrimary} onClick={handleTest} disabled={testing}>
                {testing ? '测试中...' : '开始测试'}
              </button>
            </>
          )}
        </div>
      </>
    );
  }

  return (
    <div className={styles.overlay} role="dialog" aria-label="初始化设置">
      <div className={styles.card}>
        {step === 'provider' && renderProviderStep()}
        {step === 'key' && renderKeyStep()}
        {step === 'test' && renderTestStep()}
      </div>
    </div>
  );
}
