import { FileText, FolderOpen, Save, Search } from 'lucide-react';
import { useMemo, useState } from 'react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from '../InspectorPanel.module.css';
import { useToast } from '../Toast';

function formatSize(size: number) {
  if (!size) return 'preview';
  if (size > 1024 * 1024) return `${(size / 1024 / 1024).toFixed(1)} MB`;
  if (size > 1024) return `${(size / 1024).toFixed(1)} KB`;
  return `${size} B`;
}

export default function FilesInspector() {
  const { state, dispatch } = useAppState();
  const { showToast } = useToast();
  const [openingPath, setOpeningPath] = useState<string | null>(null);
  const [loadingFileId, setLoadingFileId] = useState<string | null>(null);
  const activeFile =
    state.workspaceFiles.find((file) => file.id === state.activeEditorFileId) ?? null;

  const fileTypes = useMemo(
    () => ['all', ...Array.from(new Set(state.workspaceFiles.map((file) => file.ext).filter(Boolean)))],
    [state.workspaceFiles],
  );

  const visibleFiles = state.workspaceFiles.filter((file) => {
    const query = state.fileSearch.trim().toLowerCase();
    const matchesQuery =
      !query ||
      file.name.toLowerCase().includes(query) ||
      file.path.toLowerCase().includes(query);
    const matchesType = state.fileTypeFilter === 'all' || file.ext === state.fileTypeFilter;
    return matchesQuery && matchesType;
  });

  async function openWorkspacePath(path: string | null, reveal = false) {
    if (!path) return;
    setOpeningPath(path);
    try {
      await api.openWorkspacePath(path, reveal);
      showToast(reveal ? 'File revealed in workspace.' : 'Workspace folder opened.', 'success');
    } catch (error) {
      showToast(error instanceof Error ? error.message : 'Could not open workspace path.', 'error');
    } finally {
      setOpeningPath(null);
    }
  }

  async function openFile(fileId: string) {
    const file = state.workspaceFiles.find((item) => item.id === fileId);
    if (!file) return;
    dispatch({ type: 'OPEN_WORKSPACE_FILE', fileId });
    setLoadingFileId(fileId);
    try {
      const res = await api.readWorkspaceFile(file.path);
      dispatch({ type: 'SET_EDITOR_CONTENT', fileId, content: res.file });
      if (res.fallback) showToast('Loaded a local preview buffer while the file API is pending.', 'info');
    } catch (error) {
      showToast(error instanceof Error ? error.message : 'Could not read file.', 'error');
    } finally {
      setLoadingFileId(null);
    }
  }

  async function saveFile() {
    if (!activeFile) return;
    dispatch({ type: 'SET_EDITOR_SAVE_STATUS', status: 'saving' });
    try {
      const res = await api.saveWorkspaceFile(
        activeFile.path,
        state.editorBuffers[activeFile.id] ?? '',
        state.editorVersions[activeFile.id],
      );
      dispatch({ type: 'SET_EDITOR_SAVE_STATUS', status: 'saved' });
      dispatch({
        type: 'SET_WORKSPACE_FILES',
        files: state.workspaceFiles.map((file) =>
          file.id === activeFile.id
            ? { ...file, dirty: false, updated_at: res.file.updated_at }
            : file,
        ),
        root: state.outputDirectory ?? undefined,
        fallback: state.fallback.workspaceFiles,
      });
      if (res.fallback) showToast('Saved to the WebUI preview buffer. Backend save is pending.', 'info');
      else showToast('File saved.', 'success');
    } catch (error) {
      dispatch({
        type: 'SET_EDITOR_SAVE_STATUS',
        status: 'error',
        error: error instanceof Error ? error.message : 'Save failed.',
      });
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
            <p className={styles.muted}>Generated files will appear here after a run writes to disk.</p>
          )}
        </div>
        {state.fallback.workspaceFiles && (
          <div className={styles.notice}>Files are using WebUI preview data.</div>
        )}
        <button
          className={styles.entryButton}
          type="button"
          disabled={!state.outputDirectory}
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
            {visibleFiles.map((file) => (
              <article
                className={`${styles.fileItem} ${
                  activeFile?.id === file.id ? styles.fileItemActive : ''
                }`}
                key={file.id}
                onDoubleClick={() => openFile(file.id)}
              >
                <div>
                  <strong>{file.name}</strong>
                  <code>{file.path}</code>
                  <small>
                    {file.ext || 'file'} / {formatSize(file.size)}
                    {file.dirty ? ' / unsaved' : ''}
                  </small>
                </div>
                <div className={styles.fileActions}>
                  <button
                    className={styles.entryButton}
                    type="button"
                    aria-label={`Open ${file.name} in editor`}
                    onClick={() => openFile(file.id)}
                  >
                    <FileText size={14} aria-hidden="true" strokeWidth={2.3} />
                    {loadingFileId === file.id ? 'Loading...' : 'Edit'}
                  </button>
                  <button
                    className={styles.entryButton}
                    type="button"
                    aria-label={`Reveal ${file.name} in folder`}
                    disabled={openingPath === file.path}
                    onClick={() => openWorkspacePath(file.path, true)}
                  >
                    <FolderOpen size={14} aria-hidden="true" strokeWidth={2.3} />
                    Reveal
                  </button>
                </div>
              </article>
            ))}
          </div>
        )}
      </section>

      {activeFile && (
        <section className={styles.editorPanel}>
          <div className={styles.editorHeader}>
            <div>
              <div className={styles.sectionTitle}>Text Editor</div>
              <strong>{activeFile.name}</strong>
              <code>{activeFile.path}</code>
            </div>
            <span>{state.editorSaveStatus === 'dirty' ? 'Unsaved' : 'Local draft'}</span>
          </div>
          <textarea
            className={styles.editor}
            aria-label={`Edit ${activeFile.name}`}
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
            <button
              className={styles.entryButton}
              type="button"
              onClick={saveFile}
              disabled={state.editorSaveStatus === 'saving'}
            >
              <Save size={14} aria-hidden="true" strokeWidth={2.3} />
              {state.editorSaveStatus === 'saving' ? 'Saving...' : 'Save'}
            </button>
            <span>{state.editorError ?? state.editorSaveStatus}</span>
          </div>
        </section>
      )}
    </>
  );
}
