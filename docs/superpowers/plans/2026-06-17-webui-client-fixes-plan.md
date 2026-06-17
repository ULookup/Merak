# WebUI Client Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 6 P0 client bugs (#132-#136, #143) and introduce page-level architecture (Workbench / Settings / ChapterEditor).

**Architecture:** State-based page routing via `AppState.currentPage` — no react-router. Three pages: Workbench (existing three-column layout), Settings (merged LLM config + preferences), ChapterEditor (full-screen overlay). Dead code deleted, APIs wired.

**Tech Stack:** React 19, TypeScript, Vite, CSS Modules, Vitest

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `src/AppState.tsx` | Modify | Add `currentPage`, `activeChapterId`, `SET_PAGE`, `SET_PIPELINE_AUTO_ADVANCE` |
| `src/api/client.ts` | Modify | Extend `formatApiError()` with Chinese error mappings |
| `src/components/SettingsPanel.tsx` | Delete | Dead code — top-level user preferences panel |
| `src/components/SettingsPanel.module.css` | Delete | Dead code — companion styles |
| `src/components/SettingsPage.tsx` | Create | Merged Settings page (LLM config + user preferences) |
| `src/components/SettingsPage.module.css` | Create | Settings page styles |
| `src/App.tsx` | Modify | Page router, preferences loading in bootstrap |
| `src/App.module.css` | Modify | Add page-level styles for settings/editor |
| `src/components/WorldSidebar.tsx` | Modify | Gear button navigates to Settings page |
| `src/components/Sidebar/PipelineNavigator.tsx` | Modify | Auto/manual badge → clickable toggle |
| `src/components/Inspector/StoryInspector.tsx` | Modify | Chapter list section, time advance enable |

---

### Task 1: Add page routing and pipeline toggle to AppState

**Files:**
- Modify: `webui/src/AppState.tsx`

- [ ] **Step 1: Add `AppPage` type, new state fields, and new actions**

In `AppState.tsx`, after line 22 (`export type InspectorTab`), add:

```typescript
export type AppPage = 'workbench' | 'settings' | 'editor';
```

In the `AppState` interface (after `appPhase` on line 42), add:

```typescript
currentPage: AppPage;
activeEditorChapterId: string | null;
activeEditorChapterTitle: string;
```

In the `initialState` (after `appPhase` on line 107), add:

```typescript
currentPage: 'workbench',
activeEditorChapterId: null,
activeEditorChapterTitle: '',
```

In the `Action` type union (before the closing `;` on line 315), add:

```typescript
| { type: 'SET_PAGE'; page: AppPage }
| { type: 'OPEN_CHAPTER_EDITOR'; chapterId: string; chapterTitle: string }
| { type: 'SET_PIPELINE_AUTO_ADVANCE'; value: boolean }
```

- [ ] **Step 2: Add reducer cases**

In the `reducer` function's `switch` statement (before the `default` on line 721), add:

```typescript
case 'SET_PAGE':
  return { ...state, currentPage: action.page };

case 'OPEN_CHAPTER_EDITOR':
  return {
    ...state,
    currentPage: 'editor',
    activeEditorChapterId: action.chapterId,
    activeEditorChapterTitle: action.chapterTitle,
  };

case 'SET_PIPELINE_AUTO_ADVANCE':
  return { ...state, pipelineAutoAdvance: action.value };
```

- [ ] **Step 3: Verify TypeScript compiles**

Run: `cd webui && npx tsc --noEmit 2>&1`
Expected: No new errors.

- [ ] **Step 4: Commit**

```bash
git add webui/src/AppState.tsx
git commit -m "feat: add AppPage routing, pipeline auto-advance toggle to AppState"
```

---

### Task 2: Extend formatApiError with Chinese error mappings

**Files:**
- Modify: `webui/src/api/client.ts`

- [ ] **Step 1: Add worldbuilding Chinese error mappings**

In `formatApiError()` (line 84), replace the existing error code checks with the full set. The function should read:

```typescript
export function formatApiError(error: unknown, fallback = '操作失败，请稍后重试。') {
  if (error instanceof ApiError) {
    // Worldbuilding — world
    if (error.code === 'world_not_found') return '世界不存在或已被删除。';
    if (error.code === 'world_create_failed') return '创建世界失败，请检查名称是否重复。';
    if (error.code === 'world_name_required') return '世界名称不能为空。';

    // Worldbuilding — agent
    if (error.code === 'agent_not_found') return '角色不存在或已被删除。';
    if (error.code === 'agent_create_failed') return '创建角色失败，请检查必填字段。';
    if (error.code === 'agent_version_conflict') return '角色信息已被其他操作更新，请刷新后再试。';

    // Worldbuilding — scene
    if (error.code === 'scene_not_found') return '场景不存在或已被删除。';
    if (error.code === 'scene_create_failed') return '创建场景失败，请确认章节存在。';
    if (error.code === 'scene_end_failed') return '结束场景失败，场景可能已经结束。';
    if (error.code === 'scene_status_invalid') return '场景当前状态不支持此操作。';

    // Worldbuilding — chapter
    if (error.code === 'chapter_not_found') return '章节不存在或已被删除。';
    if (error.code === 'chapter_update_failed') return '章节更新失败，请刷新后再试。';

    // Worldbuilding — diary
    if (error.code === 'information_boundary_leak') return '日记内容包含角色不应知晓的信息，已被信息边界过滤。';
    if (error.code === 'diary_write_failed') return '日记写入失败，请稍后重试。';

    // Worldbuilding — foreshadowing / secret
    if (error.code === 'foreshadowing_not_found') return '伏笔不存在或已被删除。';
    if (error.code === 'secret_not_found') return '秘密不存在或已被删除。';
    if (error.code === 'secret_status_invalid') return '秘密当前状态不支持此操作。';

    // Pipeline
    if (error.code === 'pipeline_not_available')
      return '创作流水线暂不可用，请确认后端已启用 worldbuilding pipeline。';
    if (error.code === 'pipeline_advance_blocked')
      return '阶段推进被阻止：当前阶段条件未全部满足。请先完成当前阶段的所有要求。';
    if (error.code === 'pipeline_phase_invalid') return '目标阶段无效，请确认阶段名称正确。';

    // Image service
    if (error.code === 'image_service_not_available')
      return '图片服务未启用，请确认后端 Image Service 已初始化。';
    if (error.code === 'image_upload_failed') return '图片上传失败，请确认文件格式和大小符合要求。';
    if (error.code === 'invalid_image_type') return '图片类型必须是头像或人设图。';
    if (error.code === 'image_not_found') return '图片不存在或已被删除。';

    // General
    if (error.code === 'version_conflict') return '内容已在后端更新，请刷新后再保存。';
    if (error.code === 'file_conflict') return '文件已被其他操作修改，请刷新后再保存。';
    if (error.code === 'test_failed') return `连接测试失败：${error.message}`;
    if (error.code === 'test_unavailable') return '连接测试暂不可用，请检查后端配置。';

    return error.message || fallback;
  }
  return error instanceof Error ? error.message : fallback;
}
```

- [ ] **Step 2: Verify TypeScript compiles**

Run: `cd webui && npx tsc --noEmit 2>&1`
Expected: No errors.

- [ ] **Step 3: Commit**

```bash
git add webui/src/api/client.ts
git commit -m "fix: add Chinese error mappings for worldbuilding domain (#143)"
```

---

### Task 3: Delete dead top-level SettingsPanel files

**Files:**
- Delete: `webui/src/components/SettingsPanel.tsx`
- Delete: `webui/src/components/SettingsPanel.module.css`

- [ ] **Step 1: Delete files**

```bash
rm webui/src/components/SettingsPanel.tsx
rm webui/src/components/SettingsPanel.module.css
```

- [ ] **Step 2: Verify no import errors**

Run: `cd webui && npx tsc --noEmit 2>&1`
Expected: No errors (no other file imports these).

- [ ] **Step 3: Commit**

```bash
git add webui/src/components/SettingsPanel.tsx webui/src/components/SettingsPanel.module.css
git commit -m "fix: remove dead top-level SettingsPanel (#136)"
```

---

### Task 4: Create merged SettingsPage component

**Files:**
- Create: `webui/src/components/SettingsPage.tsx`
- Create: `webui/src/components/SettingsPage.module.css`

- [ ] **Step 1: Create SettingsPage.module.css**

```css
.page {
  position: fixed;
  inset: 0;
  z-index: 100;
  background: var(--page);
  display: flex;
  flex-direction: column;
  animation: pageIn 280ms var(--ease-spring);
}

@keyframes pageIn {
  from { opacity: 0; transform: translateY(8px); }
  to { opacity: 1; transform: translateY(0); }
}

.header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 16px 20px;
  border-bottom: 1px solid var(--border);
  background: var(--surface);
}

.backBtn {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 10px;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: var(--surface);
  color: var(--muted-strong);
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: background 120ms ease, transform 120ms ease;
}

.backBtn:hover {
  background: var(--surface-muted);
  transform: translateX(-2px);
}

.headerTitle {
  font-size: 16px;
  font-weight: 780;
  color: var(--ink);
}

.body {
  flex: 1;
  overflow-y: auto;
  padding: 24px 20px;
  max-width: 640px;
}

.section {
  margin-bottom: 28px;
}

.sectionTitle {
  font-size: 10px;
  font-weight: 720;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  color: var(--muted);
  margin-bottom: 14px;
  padding-bottom: 6px;
  border-bottom: 1px solid var(--border);
}

.field {
  margin-bottom: 14px;
}

.label {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 12px;
  font-weight: 600;
  color: var(--muted-strong);
}

.input,
.select {
  padding: 9px 11px;
  border: 1px solid var(--border);
  border-radius: 6px;
  font-size: 13px;
  background: var(--surface-strong);
  color: var(--ink);
  width: 100%;
  box-sizing: border-box;
  transition: border-color 120ms ease, box-shadow 120ms ease;
}

.input:focus,
.select:focus {
  outline: none;
  border-color: var(--ink);
  box-shadow: var(--ring);
}

.keyRow {
  display: flex;
  gap: 6px;
}

.keyRow .input {
  flex: 1;
}

.toggleBtn {
  flex-shrink: 0;
  padding: 9px 12px;
  font-size: 12px;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: var(--surface);
  color: var(--muted-strong);
  cursor: pointer;
  transition: background 120ms ease;
}

.toggleBtn:hover {
  background: var(--surface-muted);
}

.toggle {
  display: flex;
  align-items: center;
  gap: 10px;
}

.toggleSwitch {
  width: 40px;
  height: 22px;
  background: var(--border);
  border-radius: 11px;
  position: relative;
  cursor: pointer;
  transition: background 150ms ease;
  border: none;
  padding: 0;
  flex-shrink: 0;
}

.toggleSwitch[aria-checked="true"] {
  background: var(--ink);
}

.toggleKnob {
  width: 18px;
  height: 18px;
  background: #fff;
  border-radius: 50%;
  position: absolute;
  top: 2px;
  left: 2px;
  transition: transform 150ms var(--ease-spring);
}

.toggleSwitch[aria-checked="true"] .toggleKnob {
  transform: translateX(18px);
}

.toggleLabel {
  font-size: 13px;
  color: var(--ink);
}

.actions {
  display: flex;
  gap: 8px;
  margin-top: 8px;
}

.btn {
  padding: 10px 18px;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: var(--surface);
  color: var(--muted-strong);
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: background 120ms ease, transform 120ms ease;
}

.btn:hover:not(:disabled) {
  background: var(--surface-muted);
  transform: translateY(-1px);
}

.btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btnPrimary {
  composes: btn;
  background: var(--ink);
  color: #fff;
  border-color: var(--ink);
}

.btnPrimary:hover:not(:disabled) {
  background: #303030;
}

.ok {
  font-size: 13px;
  color: var(--green);
  margin-top: 8px;
}

.error {
  font-size: 13px;
  color: var(--ruby);
  margin-top: 8px;
}

.hint {
  font-size: 12px;
  color: var(--muted);
  line-height: 1.5;
  margin-bottom: 14px;
}

.runtimePanel {
  border-top: 1px solid var(--border);
  margin-top: 8px;
  padding-top: 18px;
}

.runtimeGrid {
  display: grid;
  grid-template-columns: minmax(80px, max-content) minmax(0, 1fr);
  gap: 7px 12px;
  font-size: 12px;
  margin-bottom: 14px;
}

.runtimeGrid span {
  color: var(--muted);
}

.runtimeGrid strong {
  min-width: 0;
  overflow-wrap: anywhere;
  color: var(--ink);
  font-weight: 600;
}
```

- [ ] **Step 2: Create SettingsPage.tsx**

```typescript
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
          onClick={() => dispatch({ type: 'SET_PAGE', page: 'workbench' })}
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
```

- [ ] **Step 3: Verify TypeScript compiles**

Run: `cd webui && npx tsc --noEmit 2>&1`
Expected: No errors.

- [ ] **Step 4: Commit**

```bash
git add webui/src/components/SettingsPage.tsx webui/src/components/SettingsPage.module.css
git commit -m "feat: add merged Settings page (LLM config + user preferences)"
```

---

### Task 5: Add page router and preferences loading to App.tsx

**Files:**
- Modify: `webui/src/App.tsx`
- Modify: `webui/src/App.module.css`

- [ ] **Step 1: Update App.tsx imports and rendering logic**

Add import at top (after line 12):

```typescript
import SettingsPage from './components/SettingsPage';
```

In `AppInner()`, after the `useState` declarations (after line 40), add:

```typescript
// No additional state needed — page routing is in AppState
```

In the bootstrap `useEffect` (line 43), add preferences loading alongside existing calls. After the `api.metadata()` call (line 56), add `api.getPreferences()` to the `Promise.allSettled` array. The modified section should read:

```typescript
const [metadataRes, worldsRes, sessionsRes, capabilitiesRes, prefsRes] = await Promise.allSettled([
  api.metadata(),
  api.listWorlds(),
  api.listSessions(),
  api.capabilities(),
  api.getPreferences(),
]);
```

Then after the `SET_CAPABILITIES` dispatch (after line 77), add:

```typescript
if (prefsRes.status === 'fulfilled' && prefsRes.value) {
  dispatch({
    type: 'SET_USER_PREFERENCES',
    prefs: {
      default_genre: prefsRes.value.default_genre ?? '',
      preferred_style: prefsRes.value.preferred_style ?? '轻松',
    },
  });
}
```

Replace the phase-based rendering (lines 209-291) with page-based rendering. The section from line 209 to the end of the JSX should become:

```typescript
  // Page-based rendering
  if (state.currentPage === 'settings') {
    return <SettingsPage />;
  }

  if (state.currentPage === 'editor' && state.activeEditorChapterId && state.worldId) {
    return (
      <div className={styles.editorOverlay}>
        <div className={styles.editorHeader}>
          <button
            className={styles.editorBackBtn}
            onClick={() => dispatch({ type: 'SET_PAGE', page: 'workbench' })}
          >
            <ArrowLeft size={15} aria-hidden="true" strokeWidth={2.3} />
            返回工作台
          </button>
          <h1 className={styles.editorTitle}>
            {state.activeEditorChapterTitle || '章节编辑'}
          </h1>
        </div>
        <div className={styles.editorBody}>
          <ChapterEditor chapterId={state.activeEditorChapterId} worldId={state.worldId} />
        </div>
      </div>
    );
  }

  // Workbench page (current phase-based rendering)
  if (!bootstrapped || state.appPhase === 'loading') {
    return <Skeleton />;
  }

  if (state.appPhase === 'no_world') {
    return (
      <ToastProvider>
        <WorldOnboarding onOpenGuide={() => setHelpOpen(true)} />
        <HelpDrawer open={helpOpen} onClose={() => setHelpOpen(false)} />
      </ToastProvider>
    );
  }

  if (state.appPhase === 'no_agent') {
    return (
      <ToastProvider>
        <WorldDashboard onOpenGuide={() => setHelpOpen(true)} />
        <HelpDrawer open={helpOpen} onClose={() => setHelpOpen(false)} />
      </ToastProvider>
    );
  }

  // appPhase === 'ready': three-column Workbench
  return (
    <ToastProvider>
      <div className={styles.layout}>
        <ErrorBoundary>
          <WorldSidebar open={sidebarOpen} onClose={() => setSidebarOpen(false)} />
        </ErrorBoundary>
        <ErrorBoundary>
          <div className={styles.workspace}>
            <ConnectionBanner state={connState} />
            <MainPanel
              onToggleSidebar={() => setSidebarOpen((prev) => !prev)}
              onToggleInspector={() => setInspectorOpen((prev) => !prev)}
              onOpenGuide={() => setHelpOpen(true)}
              sidebarOpen={sidebarOpen}
              inspectorOpen={inspectorOpen}
              connectionState={connState}
            />
          </div>
        </ErrorBoundary>
        <HelpDrawer open={helpOpen} onClose={() => setHelpOpen(false)} />
        <ErrorBoundary>
          <InspectorPanel open={inspectorOpen} onClose={() => setInspectorOpen(false)} />
        </ErrorBoundary>
      </div>

      {state.showSetupWizard && (
        <SetupWizard onComplete={() => dispatch({ type: 'SET_LLM_CONFIGURED', configured: true })} />
      )}

      {state.chapterReview && state.worldId && (
        <ChapterReviewBanner
          worldId={state.worldId}
          chapterId={state.chapterReview.chapter_id}
          chapterTitle={state.chapterReview.title}
          onNewChapter={() => {
            dispatch({ type: 'SET_CHAPTER_REVIEW', review: null });
            if (state.sessionId) {
              api.startRun(state.sessionId, '开始写下一章', state.selectedModel).catch(() => {});
            }
          }}
          onRevise={() => {
            dispatch({ type: 'SET_CHAPTER_REVIEW', review: null });
          }}
          onExport={() => dispatch({ type: 'SET_SHOW_EXPORT_DIALOG', show: true })}
          onClose={() => dispatch({ type: 'SET_CHAPTER_REVIEW', review: null })}
        />
      )}

      {state.showExportDialog && state.worldId && (
        <ExportDialog
          worldId={state.worldId}
          chapters={[]}
          onClose={() => dispatch({ type: 'SET_SHOW_EXPORT_DIALOG', show: false })}
        />
      )}
    </ToastProvider>
  );
```

Add ArrowLeft import at top. Change line 1 from:

```typescript
import { useEffect, useState } from 'react';
```

To:

```typescript
import { useEffect, useState } from 'react';
import { ArrowLeft } from 'lucide-react';
```

Add ChapterEditor import. After the existing imports (after line 13), add:

```typescript
import ChapterEditor from './components/ChapterEditor';
```

- [ ] **Step 2: Add CSS for editor overlay and page transitions in App.module.css**

Append to `webui/src/App.module.css`:

```css
/* Editor Overlay */
.editorOverlay {
  position: fixed;
  inset: 0;
  z-index: 200;
  background: var(--page);
  display: flex;
  flex-direction: column;
  animation: pageIn 280ms var(--ease-spring);
}

.editorHeader {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 16px 20px;
  border-bottom: 1px solid var(--border);
  background: var(--surface);
  flex-shrink: 0;
}

.editorBackBtn {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 10px;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: var(--surface);
  color: var(--muted-strong);
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: background 120ms ease, transform 120ms ease;
}

.editorBackBtn:hover {
  background: var(--surface-muted);
  transform: translateX(-2px);
}

.editorTitle {
  font-size: 16px;
  font-weight: 780;
  color: var(--ink);
}

.editorBody {
  flex: 1;
  overflow-y: auto;
  padding: 0;
}

@keyframes pageIn {
  from { opacity: 0; transform: translateY(8px); }
  to { opacity: 1; transform: translateY(0); }
}
```

- [ ] **Step 3: Verify TypeScript compiles**

Run: `cd webui && npx tsc --noEmit 2>&1`
Expected: No errors.

- [ ] **Step 4: Commit**

```bash
git add webui/src/App.tsx webui/src/App.module.css
git commit -m "feat: add page router, preferences bootstrap, chapter editor overlay (#132, #135)"
```

---

### Task 6: Update WorldSidebar gear button to navigate to Settings page

**Files:**
- Modify: `webui/src/components/WorldSidebar.tsx`

- [ ] **Step 1: Change settings trigger to page navigation**

In WorldSidebar.tsx, remove line 47 (`const [settingsOpen, setSettingsOpen] = useState(false);`).

Remove the import on line 25 (`import SettingsPanel from './Sidebar/SettingsPanel';`).

Replace the settings trigger (lines 167-177) with:

```typescript
      <div
        className={styles.settingsTrigger}
        onClick={() => dispatch({ type: 'SET_PAGE', page: 'settings' })}
        role="button"
        tabIndex={0}
        onKeyDown={(e) => { if (e.key === 'Enter') dispatch({ type: 'SET_PAGE', page: 'settings' }); }}
      >
        <Settings size={15} aria-hidden="true" strokeWidth={2.3} />
        Settings
      </div>
```

- [ ] **Step 2: Verify TypeScript compiles**

Run: `cd webui && npx tsc --noEmit 2>&1`
Expected: No errors.

- [ ] **Step 3: Commit**

```bash
git add webui/src/components/WorldSidebar.tsx
git commit -m "feat: navigate to Settings page from sidebar gear button"
```

---

### Task 7: Make pipeline auto-advance badge a clickable toggle

**Files:**
- Modify: `webui/src/components/Sidebar/PipelineNavigator.tsx`

- [ ] **Step 1: Make badge clickable**

In PipelineNavigator.tsx, replace lines 113-119 (the badge rendering in titleRow) with:

```typescript
      <div className={styles.titleRow}>
        <span className={styles.title}>故事流水线</span>
        <button
          type="button"
          className={state.pipelineAutoAdvance ? styles.badgeAuto : styles.badgeManual}
          onClick={() =>
            dispatch({
              type: 'SET_PIPELINE_AUTO_ADVANCE',
              value: !state.pipelineAutoAdvance,
            })
          }
          title={state.pipelineAutoAdvance ? '点击切换为手动推进' : '点击切换为自动推进'}
        >
          {state.pipelineAutoAdvance ? '自动' : '手动'}
        </button>
      </div>
```

- [ ] **Step 2: Add cursor style to badge classes in CSS**

In `webui/src/components/Sidebar/PipelineNavigator.module.css`, update `.badgeAuto` and `.badgeManual` to add `cursor: pointer; border: none;`:

```css
.badgeAuto {
  cursor: pointer;
  border: none;
  font-size: 10px;
  font-weight: 600;
  background: var(--surface, #fff);
  border: 1px solid var(--border, #e0dfdc);
  color: var(--ink, #171717);
  padding: 2px 8px;
  border-radius: 999px;
  transition: background 120ms ease;
}

.badgeAuto:hover {
  background: var(--surface-muted, #f3f3f1);
}

.badgeManual {
  cursor: pointer;
  border: none;
  font-size: 10px;
  font-weight: 600;
  background: var(--amber-soft, #fff4d6);
  color: var(--amber, #a35c00);
  padding: 2px 8px;
  border-radius: 999px;
  transition: background 120ms ease;
}

.badgeManual:hover {
  filter: brightness(0.95);
}
```

- [ ] **Step 3: Verify TypeScript compiles**

Run: `cd webui && npx tsc --noEmit 2>&1`
Expected: No errors.

- [ ] **Step 4: Commit**

```bash
git add webui/src/components/Sidebar/PipelineNavigator.tsx webui/src/components/Sidebar/PipelineNavigator.module.css
git commit -m "feat: make pipeline auto-advance badge a clickable toggle (#134)"
```

---

### Task 8: Add chapter list and enable time advance in StoryInspector

**Files:**
- Modify: `webui/src/components/Inspector/StoryInspector.tsx`

- [ ] **Step 1: Add chapters section and wire time advance**

In StoryInspector.tsx:

Add imports at top:

```typescript
import { AlertTriangle, BookOpen, ChevronRight, Clock3, Edit3, Flag, GitBranch, KeyRound, Plus, Users } from 'lucide-react';
```

Add type import — update line 4 from:
```typescript
import type { DiaryEntry, StoryScene, WorldDetail } from '../../api/types';
```
to:
```typescript
import type { DiaryEntry, StoryChapter, StoryScene, WorldDetail } from '../../api/types';
```

Add state variables after line 80 (after `const [worldDetail, setWorldDetail] = ...`):

```typescript
  const [chapters, setChapters] = useState<StoryChapter[]>([]);
  const [chaptersLoading, setChaptersLoading] = useState(false);
  const [newWorldTime, setNewWorldTime] = useState('');
  const [advancingTime, setAdvancingTime] = useState(false);
```

Add chapters loading effect (after the worldDetail `useEffect` on line 90):

```typescript
  useEffect(() => {
    if (!state.worldId) return;
    let cancelled = false;
    setChaptersLoading(true);
    api.listChapters(state.worldId)
      .then((res) => {
        if (!cancelled) setChapters(res.chapters ?? []);
      })
      .catch(() => {})
      .finally(() => {
        if (!cancelled) setChaptersLoading(false);
      });
    return () => { cancelled = true; };
  }, [state.worldId, state.storyVersion]);
```

Replace the time advance section (lines 186-199) with this enabled version:

```typescript
        <div className={styles.timeAdvance}>
          <div>
            <strong>世界时间</strong>
            <span>{state.worldTime ? `当前：${state.worldTime}` : '时间未设定'}</span>
          </div>
          <div className={styles.timeAdvanceRow}>
            <input
              className={styles.timeInput}
              type="text"
              value={newWorldTime}
              onChange={(e) => setNewWorldTime(e.target.value)}
              placeholder="例如：第三章·清晨"
              disabled={advancingTime}
            />
            <button
              className={styles.ghostButton}
              disabled={advancingTime || !newWorldTime.trim()}
              onClick={async () => {
                if (!state.worldId || !newWorldTime.trim()) return;
                setAdvancingTime(true);
                try {
                  await api.advanceWorldTime(state.worldId, newWorldTime.trim());
                  dispatch({ type: 'SET_STORY_VERSION' });
                  setNewWorldTime('');
                } catch {
                  // Error handled by toast/global error
                } finally {
                  setAdvancingTime(false);
                }
              }}
              title="推进世界时间"
            >
              <Clock3 size={14} aria-hidden="true" />
              {advancingTime ? '推进中...' : '推进时间'}
            </button>
          </div>
        </div>
```

After the existing "Knowledge Boundaries" section (before line 371), add the chapters section:

```typescript
      {/* Chapters List */}
      <section className={styles.section}>
        <div className={styles.sectionHeader}>
          <div className={styles.sectionTitle}>
            <BookOpen size={14} aria-hidden="true" />
            章节目录
          </div>
        </div>
        {chaptersLoading ? (
          <p className={styles.muted}>加载中...</p>
        ) : chapters.length === 0 ? (
          <p className={styles.muted}>暂无章节。</p>
        ) : (
          chapters.map((ch) => (
            <div
              className={styles.thread}
              key={ch.id}
              style={{ cursor: 'pointer' }}
              onClick={() =>
                dispatch({
                  type: 'OPEN_CHAPTER_EDITOR',
                  chapterId: ch.id,
                  chapterTitle: `第${ch.number}章 ${ch.title}`,
                })
              }
              role="button"
              tabIndex={0}
              onKeyDown={(e) => {
                if (e.key === 'Enter')
                  dispatch({
                    type: 'OPEN_CHAPTER_EDITOR',
                    chapterId: ch.id,
                    chapterTitle: `第${ch.number}章 ${ch.title}`,
                  });
              }}
            >
              <span>
                <Edit3 size={12} aria-hidden="true" style={{ marginRight: 6 }} />
                第{ch.number}章 {ch.title}
              </span>
              <small>
                {ch.status?.replace(/_/g, ' ') ?? 'draft'} · {ch.scene_count} 个场景
                <ChevronRight size={12} aria-hidden="true" style={{ marginLeft: 4 }} />
              </small>
            </div>
          ))
        )}
      </section>
```

- [ ] **Step 2: Add CSS for time advance input in InspectorPanel.module.css**

Check if `webui/src/components/InspectorPanel.module.css` needs new styles. Append:

```css
.timeAdvance {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 12px;
  background: var(--surface-muted);
  border-radius: 6px;
  margin-top: 8px;
}

.timeAdvance strong {
  font-size: 12px;
  font-weight: 700;
  color: var(--ink);
}

.timeAdvance span {
  display: block;
  font-size: 11px;
  color: var(--muted);
  margin-top: 2px;
}

.timeAdvanceRow {
  display: flex;
  gap: 6px;
}

.timeInput {
  flex: 1;
  padding: 7px 10px;
  border: 1px solid var(--border);
  border-radius: 6px;
  font-size: 13px;
  background: var(--surface);
  color: var(--ink);
}

.timeInput:focus {
  outline: none;
  border-color: var(--ink);
  box-shadow: var(--ring);
}

.timeInput:disabled {
  opacity: 0.5;
}
```

- [ ] **Step 3: Verify TypeScript compiles**

Run: `cd webui && npx tsc --noEmit 2>&1`
Expected: No errors.

- [ ] **Step 4: Commit**

```bash
git add webui/src/components/Inspector/StoryInspector.tsx webui/src/components/InspectorPanel.module.css
git commit -m "feat: add chapter list, enable world time advance (#132, #133)"
```

---

### Task 9: Final integration verification

- [ ] **Step 1: Run full TypeScript check**

```bash
cd webui && npx tsc --noEmit 2>&1
```

- [ ] **Step 2: Run linter**

```bash
cd webui && npm run lint 2>&1
```

- [ ] **Step 3: Run tests**

```bash
cd webui && npm run test 2>&1
```

- [ ] **Step 4: Fix any failures, then commit remaining changes**

```bash
git add -A && git status
```

---

### Task 10: Close false-positive issue #137

- [ ] **Step 1: Close #137 with explanation**

```bash
gh issue close 137 --comment "False positive — ModalBase.module.css is imported via CSS Modules 'composes' syntax in CreateModal.module.css and EndSceneModal.module.css. This is standard CSS Modules composition." 2>&1
```

- [ ] **Step 2: Close fixed issues**

```bash
gh issue close 136 --comment "Fixed — dead top-level SettingsPanel files deleted."
gh issue close 135 --comment "Fixed — user preferences loaded in App bootstrap and Settings page."
gh issue close 134 --comment "Fixed — pipeline auto-advance badge is now a clickable toggle."
gh issue close 133 --comment "Fixed — world time advance button enabled with input field."
gh issue close 132 --comment "Fixed — ChapterEditor integrated via chapter list in StoryInspector, opens as full-screen overlay."
gh issue close 143 --comment "Fixed — 20+ Chinese error mappings added for worldbuilding domain."
```

---

### Quick Verification Checklist

- [ ] Workbench loads as default page
- [ ] Settings gear in sidebar → navigates to Settings page
- [ ] Settings page: LLM config saves/loads correctly
- [ ] Settings page: User preferences save/load correctly
- [ ] Back button in Settings → returns to Workbench
- [ ] StoryInspector shows chapters list
- [ ] Click chapter → full-screen ChapterEditor opens
- [ ] ChapterEditor: Esc or back button → returns to Workbench
- [ ] ChapterEditor: Ctrl+S saves
- [ ] World time advance: enter time, click button, time updates
- [ ] Pipeline badge toggles between auto/manual on click
- [ ] Error messages display in Chinese for known codes
