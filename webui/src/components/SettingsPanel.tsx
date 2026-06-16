import { useState, useEffect } from 'react';
import { api, formatApiError } from '../api/client';
import { useAppState } from '../AppState';
import styles from './SettingsPanel.module.css';

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

export default function SettingsPanel() {
  const [genre, setGenre] = useState('无偏好');
  const { dispatch } = useAppState();
  const [style, setStyle] = useState('轻松');
  const [allowUsageLogs, setAllowUsageLogs] = useState(true);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [fetchError, setFetchError] = useState<string | null>(null);
  const [savedMsg, setSavedMsg] = useState(false);

  useEffect(() => {
    let cancelled = false;

    async function load() {
      setLoading(true);
      setFetchError(null);

      try {
        const prefs = await api.getPreferences();
        if (cancelled) return;

        if (prefs.default_genre) setGenre(prefs.default_genre);
        if (prefs.preferred_style) setStyle(prefs.preferred_style);
        if (prefs.allow_usage_logs !== undefined) setAllowUsageLogs(prefs.allow_usage_logs);
        dispatch({ type: 'SET_USER_PREFERENCES', prefs: { default_genre: prefs.default_genre ?? '', preferred_style: prefs.preferred_style ?? '轻松' } });
      } catch (e: unknown) {
        if (cancelled) return;
        setFetchError(formatApiError(e, '加载偏好设置失败，请稍后重试。'));
      } finally {
        if (!cancelled) setLoading(false);
      }
    }

    load();

    return () => {
      cancelled = true;
    };
  }, []);

  async function handleSave() {
    setSaving(true);
    setError(null);
    setSavedMsg(false);

    try {
      await api.savePreferences({
        default_genre: genre,
        preferred_style: style,
        allow_usage_logs: allowUsageLogs,
      });
      setSavedMsg(true);
      setTimeout(() => setSavedMsg(false), 2000);
    } catch (e: unknown) {
      setError(formatApiError(e, '保存偏好设置失败，请稍后重试。'));
    } finally {
      setSaving(false);
    }
  }

  if (loading) {
    return <div className={styles.loading}>正在加载偏好设置...</div>;
  }

  return (
    <div className={styles.panel}>
      {fetchError && <div className={styles.error}>{fetchError}</div>}

      <div className={styles.section}>
        <h3 className={styles.sectionTitle}>创作偏好</h3>

        <div className={styles.field}>
          <label className={styles.label}>默认题材</label>
          <select
            className={styles.select}
            value={genre}
            onChange={(e) => {
              setGenre(e.target.value);
              setSavedMsg(false);
            }}
            disabled={saving}
          >
            {GENRE_OPTIONS.map((g) => (
              <option key={g.value} value={g.value}>
                {g.label}
              </option>
            ))}
          </select>
        </div>

        <div className={styles.field}>
          <label className={styles.label}>偏好风格</label>
          <select
            className={styles.select}
            value={style}
            onChange={(e) => {
              setStyle(e.target.value);
              setSavedMsg(false);
            }}
            disabled={saving}
          >
            {STYLE_OPTIONS.map((s) => (
              <option key={s.value} value={s.value}>
                {s.label}
              </option>
            ))}
          </select>
        </div>

        <div className={styles.field}>
          <label className={styles.label}>使用情况日志</label>
          <div className={styles.toggle}>
            <button
              type="button"
              className={`${styles.toggleSwitch} ${allowUsageLogs ? styles.toggleSwitchOn : ''}`}
              onClick={() => {
                setAllowUsageLogs((prev) => !prev);
                setSavedMsg(false);
              }}
              disabled={saving}
              aria-checked={allowUsageLogs}
              role="switch"
              aria-label="切换使用情况日志"
            >
              <span
                className={`${styles.toggleKnob} ${allowUsageLogs ? styles.toggleKnobOn : ''}`}
              />
            </button>
            <span className={styles.toggleLabel}>
              {allowUsageLogs ? '已开启' : '已关闭'}
            </span>
          </div>
        </div>
      </div>

      <div className={styles.section}>
        <h3 className={styles.sectionTitle}>大模型</h3>
        <p className={styles.hint}>
          LLM 提供商与模型配置请通过“初始化设置”向导进行设置。
          设置将包含 API 密钥、模型名称、温度与上下文记忆长度等选项。
        </p>
      </div>

      <div className={styles.actions}>
        <button
          className={styles.btnPrimary}
          onClick={handleSave}
          disabled={saving}
        >
          {saving ? '保存中...' : '保存偏好'}
        </button>
      </div>

      {error && <div className={styles.error}>{error}</div>}
      {savedMsg && <div className={styles.successMsg}>已保存</div>}
    </div>
  );
}
