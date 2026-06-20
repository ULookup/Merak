import { useEffect, useMemo, useRef, useState } from 'react';
import {
  AlertCircle,
  CheckCircle2,
  FileText,
  FolderOpen,
  RotateCcw,
  Save,
  Search,
} from 'lucide-react';
import { api, formatApiError } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from '../InspectorPanel.module.css';
import { useToast } from '../Toast';

function formatSize(size: number) {
  if (!size) return '0 B';
  if (size > 1024 * 1024) return `${(size / 1024 / 1024).toFixed(1)} MB`;
  if (size > 1024) return `${(size / 1024).toFixed(1)} KB`;
  return `${size} B`;
}

function displayFileName(name: string) {
  return name.replace(/\.(md|markdown|txt|docx|json|ya?ml)$/i, '');
}

function formatUpdated(value: string | undefined) {
  if (!value) return 'Not saved yet';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleString(undefined, {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  });
}

export default function FilesInspector() {
  const { state, dispatch } = useAppState();
  const { showToast } = useToast();
  const [openingPath, setOpeningPath] = useState<string | null>(null);
  const [loadingFileId, setLoadingFileId] = useState<string | null>(null);
  const [conflictFileId, setConflictFileId] = useState<string | null>(null);
  const lifecycleGeneration = useRef(0);
  const readGeneration = useRef(0);
  const mounted = useRef(true);
  useEffect(() => {
    mounted.current = true;
    const lifecycle = ++lifecycleGeneration.current;
    return () => {
      mounted.current = false;
      if (lifecycleGeneration.current === lifecycle) lifecycleGeneration.current += 1;
      readGeneration.current += 1;
    };
  }, []);
  const activeFile =
    state.workspaceFiles.find((file) => file.id === state.activeEditorFileId) ?? null;
  const activeFileName = activeFile ? displayFileName(activeFile.name) : '';
  const activeFileDirty = Boolean(activeFile?.dirty || state.editorSaveStatus === 'dirty');
  const activeFileLoaded = activeFile ? activeFile.id in state.editorOriginals : false;
  const editorStatusLabel =
    state.editorSaveStatus === 'saving'
      ? 'Saving...'
      : state.editorSaveStatus === 'error'
        ? 'Save failed'
        : activeFileDirty
          ? 'Unsaved changes'
          : state.editorSaveStatus === 'saved'
            ? 'Saved'
            : 'Local draft';
  const editorStatusIcon =
    state.editorSaveStatus === 'error' ? (
      <AlertCircle size={13} aria-hidden="true" strokeWidth={2.3} />
    ) : state.editorSaveStatus === 'saved' ? (
      <CheckCircle2 size={13} aria-hidden="true" strokeWidth={2.3} />
    ) : null;

  const fileTypes = useMemo(
    () => [
      'all',
      ...Array.from(new Set(state.workspaceFiles.map((file) => file.ext).filter(Boolean))),
    ],
    [state.workspaceFiles],
  );

  const visibleFiles = state.workspaceFiles.filter((file) => {
    const query = state.fileSearch.trim().toLowerCase();
    const matchesQuery =
      !query || file.name.toLowerCase().includes(query) || file.path.toLowerCase().includes(query);
    const matchesType = state.fileTypeFilter === 'all' || file.ext === state.fileTypeFilter;
    return matchesQuery && matchesType;
  });

  async function openWorkspacePath(path: string | null, reveal = false) {
    if (!path || state.editorSaveStatus === 'saving') return;
    setOpeningPath(path);
    try {
      await api.openWorkspacePath(path, reveal);
      showToast(reveal ? 'File revealed in workspace.' : 'Workspace folder opened.', 'success');
    } catch (error) {
      showToast(formatApiError(error, 'Could not open workspace path.'), 'error');
    } finally {
      setOpeningPath(null);
    }
  }

  async function openFile(fileId: string) {
    if (state.editorSaveStatus === 'saving') return;
    const file = state.workspaceFiles.find((item) => item.id === fileId);
    if (!file) return;
    dispatch({ type: 'OPEN_WORKSPACE_FILE', fileId });
    const lifecycle = lifecycleGeneration.current;
    const token = ++readGeneration.current;
    setConflictFileId(null);
    setLoadingFileId(fileId);
    try {
      const res = await api.readWorkspaceFile(file.path);
      if (
        mounted.current &&
        lifecycle === lifecycleGeneration.current &&
        token === readGeneration.current
      )
        dispatch({ type: 'SET_EDITOR_CONTENT', fileId, content: res.file });
    } catch (error) {
      if (
        mounted.current &&
        lifecycle === lifecycleGeneration.current &&
        token === readGeneration.current
      )
        showToast(formatApiError(error, 'Could not read file.'), 'error');
    } finally {
      if (
        mounted.current &&
        lifecycle === lifecycleGeneration.current &&
        token === readGeneration.current
      )
        setLoadingFileId(null);
    }
  }

  async function saveFile() {
    if (!activeFile) return;
    const target = activeFile;
    const lifecycle = lifecycleGeneration.current;
    dispatch({ type: 'SET_EDITOR_SAVE_STATUS', status: 'saving' });
    try {
      const res = await api.saveWorkspaceFile(
        target.path,
        state.editorBuffers[target.id] ?? '',
        state.editorVersions[target.id],
      );
      if (!mounted.current || lifecycle !== lifecycleGeneration.current) return;
      dispatch({ type: 'SET_EDITOR_SAVE_STATUS', status: 'saved' });
      dispatch({ type: 'COMMIT_EDITOR_BUFFER', fileId: target.id, version: res.file.version });
      dispatch({
        type: 'SET_WORKSPACE_FILES',
        files: state.workspaceFiles.map((file) =>
          file.id === target.id ? { ...file, dirty: false, updated_at: res.file.updated_at } : file,
        ),
        root: state.outputDirectory ?? undefined,
        fallback: state.fallback.workspaceFiles,
      });
      showToast('File saved.', 'success');
    } catch (error) {
      if (!mounted.current || lifecycle !== lifecycleGeneration.current) return;
      const conflict = error as { status?: number; code?: string };
      if (conflict.status === 409 || conflict.code === 'file_conflict')
        setConflictFileId(target.id);
      dispatch({
        type: 'SET_EDITOR_SAVE_STATUS',
        status: 'error',
        error: formatApiError(error, 'Save failed.'),
      });
      showToast(formatApiError(error, 'Save failed.'), 'error');
    }
  }

  return (
    <>
      <section className={styles.outputCard}>
        <div>
          <div className={styles.sectionTitle}>Output Directory</div>
          {state.outputDirectory ? (
            <code className={styles.pathLine}>{state.outputDirectory}</code>
          ) : (
            <p className={styles.muted}>
              Generated files will appear here after a run writes to disk.
            </p>
          )}
        </div>
        <button
          className={styles.entryButton}
          type="button"
          disabled={!state.outputDirectory || state.editorSaveStatus === 'saving'}
          aria-label="Open output folder"
          onClick={() => openWorkspacePath(state.outputDirectory)}
        >
          <FolderOpen size={14} aria-hidden="true" strokeWidth={2.3} />
          {openingPath === state.outputDirectory ? 'Opening...' : 'Open folder'}
        </button>
      </section>

      <section className={styles.section}>
        <div className={styles.sectionTitle}>Generated Files</div>
        <div className={styles.fileToolbar}>
          <label>
            <Search size={14} aria-hidden="true" />
            <input
              value={state.fileSearch}
              onChange={(event) => dispatch({ type: 'SET_FILE_SEARCH', value: event.target.value })}
              placeholder="Search files"
            />
          </label>
          <select
            disabled={state.editorSaveStatus === 'saving'}
            value={state.fileTypeFilter}
            onChange={(event) =>
              dispatch({ type: 'SET_FILE_TYPE_FILTER', value: event.target.value })
            }
            aria-label="Filter by file type"
          >
            {fileTypes.map((type) => (
              <option key={type} value={type}>
                {type === 'all' ? 'All' : `.${type}`}
              </option>
            ))}
          </select>
        </div>

        {visibleFiles.length === 0 ? (
          <p className={styles.muted}>No files match this view yet.</p>
        ) : (
          <div className={styles.fileList}>
            {visibleFiles.map((file) =>
              (() => {
                const displayName = displayFileName(file.name);
                return (
                  <article
                    className={`${styles.fileItem} ${
                      activeFile?.id === file.id ? styles.fileItemActive : ''
                    }`}
                    key={file.id}
                    onDoubleClick={() => openFile(file.id)}
                  >
                    <div>
                      <strong>{displayName}</strong>
                      <code>{file.path}</code>
                      <small>
                        {file.ext || 'file'} / {formatSize(file.size)} / Updated{' '}
                        {formatUpdated(file.updated_at)}
                      </small>
                      {file.dirty && <span className={styles.fileDirtyBadge}>Unsaved changes</span>}
                    </div>
                    <div className={styles.fileActions}>
                      <button
                        className={styles.entryButton}
                        type="button"
                        aria-label={`Open ${displayName} in editor`}
                        disabled={state.editorSaveStatus === 'saving'}
                        onClick={() => openFile(file.id)}
                      >
                        <FileText size={14} aria-hidden="true" strokeWidth={2.3} />
                        {loadingFileId === file.id ? 'Loading...' : 'Edit'}
                      </button>
                      <button
                        className={styles.entryButton}
                        type="button"
                        aria-label={`Reveal ${displayName} in folder`}
                        disabled={openingPath === file.path || state.editorSaveStatus === 'saving'}
                        onClick={() => openWorkspacePath(file.path, true)}
                      >
                        <FolderOpen size={14} aria-hidden="true" strokeWidth={2.3} />
                        Reveal
                      </button>
                    </div>
                  </article>
                );
              })(),
            )}
          </div>
        )}
      </section>

      {activeFile && (
        <section className={styles.editorPanel}>
          <div className={styles.editorHeader}>
            <div>
              <div className={styles.sectionTitle}>Text Editor</div>
              <strong>{activeFileName}</strong>
              <code>{activeFile.path}</code>
              <small className={styles.editorMeta}>
                Last loaded{' '}
                {activeFileLoaded ? formatUpdated(activeFile.updated_at) : 'from preview buffer'}
              </small>
            </div>
            <span
              className={`${styles.editorStatus} ${
                state.editorSaveStatus === 'error'
                  ? styles.editorStatusError
                  : activeFileDirty
                    ? styles.editorStatusDirty
                    : state.editorSaveStatus === 'saved'
                      ? styles.editorStatusSaved
                      : ''
              }`}
            >
              {editorStatusIcon}
              {editorStatusLabel}
            </span>
          </div>
          <textarea
            className={styles.editor}
            aria-label={`Edit ${activeFileName}`}
            value={state.editorBuffers[activeFile.id] ?? ''}
            onChange={(event) =>
              dispatch({
                type: 'UPDATE_EDITOR_BUFFER',
                fileId: activeFile.id,
                content: event.target.value,
              })
            }
          />
          <div className={styles.editorActions}>
            <div className={styles.editorButtonGroup}>
              <button
                className={styles.entryButton}
                type="button"
                aria-label={`Save ${activeFileName}`}
                onClick={saveFile}
                disabled={state.editorSaveStatus === 'saving' || !activeFileDirty}
              >
                <Save size={14} aria-hidden="true" strokeWidth={2.3} />
                {state.editorSaveStatus === 'saving' ? 'Saving...' : 'Save'}
              </button>
              <button
                className={styles.secondaryButton}
                type="button"
                aria-label={`Revert ${activeFileName} to last loaded version`}
                disabled={!activeFileDirty || !activeFileLoaded}
                onClick={() => dispatch({ type: 'REVERT_EDITOR_BUFFER', fileId: activeFile.id })}
              >
                <RotateCcw size={14} aria-hidden="true" strokeWidth={2.3} />
                Revert
              </button>
            </div>
            <span className={state.editorError ? styles.editorErrorText : ''}>
              {state.editorError ??
                (activeFileDirty ? 'Changes stay local until saved.' : 'Ready.')}
            </span>
          </div>
          {conflictFileId === activeFile.id && (
            <div className={styles.editorActions} role="alert">
              <span>The file changed on disk. Your local draft and version are preserved.</span>
              <button
                type="button"
                className={styles.secondaryButton}
                onClick={() => openFile(activeFile.id)}
              >
                Reload
              </button>
              <button
                type="button"
                className={styles.secondaryButton}
                onClick={() =>
                  navigator.clipboard?.writeText(state.editorBuffers[activeFile.id] ?? '')
                }
              >
                Copy
              </button>
            </div>
          )}
        </section>
      )}
    </>
  );
}
