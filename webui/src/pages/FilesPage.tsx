import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { api, formatApiError } from '../api/client';
import { filesApi } from '../api/files';
import type { WorkspaceFile, WorkspaceFileContent, WorldFileLinkRecord } from '../api/types';
import DetailPane from '../components/layout/DetailPane';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import styles from './FilesPage.module.css';

type Props = { worldId: string };
type FileType = 'all' | 'markdown' | 'text' | 'data';
type LinksState = {
  status: 'loading' | 'ready' | 'error';
  items: WorldFileLinkRecord[];
  error?: string;
};

function isConflict(error: unknown) {
  const value = error as { status?: number; code?: string };
  return value?.status === 409 || value?.code === 'file_conflict';
}

function matchesType(file: WorkspaceFile, type: FileType) {
  if (type === 'all') return true;
  const ext = file.ext.toLowerCase();
  if (type === 'markdown') return ['md', 'markdown'].includes(ext);
  if (type === 'text') return ['txt', 'text'].includes(ext);
  return ['json', 'yaml', 'yml', 'csv', 'tsv'].includes(ext);
}

export default function FilesPage({ worldId }: Props) {
  const [files, setFiles] = useState<WorkspaceFile[]>([]);
  const [root, setRoot] = useState('');
  const [links, setLinks] = useState<LinksState>({ status: 'loading', items: [] });
  const [selectedPath, setSelectedPath] = useState<string | null>(null);
  const [loaded, setLoaded] = useState<WorkspaceFileContent | null>(null);
  const [draft, setDraft] = useState('');
  const [query, setQuery] = useState('');
  const [type, setType] = useState<FileType>('all');
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [clipboardError, setClipboardError] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const [conflict, setConflict] = useState(false);
  const lifecycle = useRef(0);
  const listToken = useRef(0);
  const readToken = useRef(0);
  const saveToken = useRef(0);
  const savingRef = useRef(false);

  const loadList = useCallback(async () => {
    if (savingRef.current) return;
    const life = lifecycle.current;
    const token = ++listToken.current;
    setLoading(true);
    setError(null);
    try {
      const result = await api.listWorkspaceFiles({
        world_id: worldId,
        type: type === 'all' ? undefined : type,
      });
      if (life !== lifecycle.current || token !== listToken.current) return;
      setFiles(result.files);
      setRoot(result.root);
      setSelectedPath(null);
      setLoaded(null);
    } catch (reason) {
      if (life === lifecycle.current && token === listToken.current)
        setError(formatApiError(reason, 'Could not load files.'));
    } finally {
      if (life === lifecycle.current && token === listToken.current) setLoading(false);
    }
  }, [type, worldId]);

  useEffect(() => {
    lifecycle.current += 1;
    savingRef.current = false;
    setSaving(false);
    setConflict(false);
    setLinks({ status: 'loading', items: [] });
    loadList();
    const life = lifecycle.current;
    filesApi
      .listWorldFiles(worldId)
      .then((result) => {
        if (life === lifecycle.current)
          setLinks({ status: 'ready', items: result.items ?? result.files ?? [] });
      })
      .catch((reason) => {
        if (life === lifecycle.current)
          setLinks({
            status: 'error',
            items: [],
            error: formatApiError(reason, 'Request failed.'),
          });
      });
    return () => {
      lifecycle.current += 1;
      savingRef.current = false;
    };
  }, [loadList, worldId]);

  const visible = useMemo(() => {
    const needle = query.trim().toLowerCase();
    return files.filter(
      (file) =>
        matchesType(file, type) &&
        (!needle ||
          file.name.toLowerCase().includes(needle) ||
          file.path.toLowerCase().includes(needle)),
    );
  }, [files, query, type]);

  async function selectFile(path: string) {
    if (savingRef.current) return;
    const life = lifecycle.current;
    const token = ++readToken.current;
    setSelectedPath(path);
    setLoaded(null);
    setConflict(false);
    setError(null);
    setClipboardError(null);
    try {
      const result = await api.readWorkspaceFile(path);
      if (life !== lifecycle.current || token !== readToken.current) return;
      setLoaded(result.file);
      setDraft(result.file.content);
    } catch (reason) {
      if (life === lifecycle.current && token === readToken.current)
        setError(formatApiError(reason, 'Could not read file.'));
    }
  }

  async function saveFile() {
    if (!loaded || savingRef.current) return;
    const life = lifecycle.current;
    const token = ++saveToken.current;
    const target = loaded.path;
    const content = draft;
    const version = loaded.version;
    savingRef.current = true;
    setSaving(true);
    setError(null);
    setConflict(false);
    try {
      const result = await api.saveWorkspaceFile(target, content, version);
      if (life !== lifecycle.current || token !== saveToken.current) return;
      setLoaded((current) =>
        current?.path === target
          ? {
              ...current,
              content,
              version: result.file.version,
              updated_at: result.file.updated_at,
            }
          : current,
      );
    } catch (reason) {
      if (life !== lifecycle.current || token !== saveToken.current) return;
      setConflict(isConflict(reason));
      setError(formatApiError(reason, 'Save failed.'));
    } finally {
      if (life === lifecycle.current && token === saveToken.current) {
        savingRef.current = false;
        setSaving(false);
      }
    }
  }

  async function copyDraft() {
    setClipboardError(null);
    try {
      if (!navigator.clipboard?.writeText) throw new Error('Clipboard is unavailable.');
      await navigator.clipboard.writeText(draft);
    } catch (reason) {
      setClipboardError(formatApiError(reason, 'Could not copy the local draft.'));
    }
  }

  const selected = files.find((file) => file.path === selectedPath) ?? null;
  const linked = selected ? links.items.filter((link) => link.file_path === selected.path) : [];
  if (loading) return <PageState loading loadingLabel="Loading files" />;
  if (error && files.length === 0) return <PageState error={new Error(error)} onRetry={loadList} />;
  return (
    <div className={styles.page}>
      <aside className={styles.library} aria-label="File library">
        <header>
          <h1>Files</h1>
          <button type="button" aria-label="Refresh files" disabled={saving} onClick={loadList}>
            Refresh
          </button>
        </header>
        {root && (
          <div className={styles.root}>
            <code>{root}</code>
            <button type="button" onClick={() => api.openWorkspacePath(root)}>
              Open workspace
            </button>
          </div>
        )}
        <input
          aria-label="Search files"
          placeholder="Search files"
          value={query}
          onChange={(event) => setQuery(event.target.value)}
        />
        <select
          aria-label="File type"
          value={type}
          disabled={saving}
          onChange={(event) => setType(event.target.value as FileType)}
        >
          <option value="all">All</option>
          <option value="markdown">Markdown</option>
          <option value="text">Text</option>
          <option value="data">Data</option>
        </select>
        {visible.length ? (
          <ResourceList
            disabled={saving}
            items={visible}
            selectedId={selectedPath}
            getId={(file) => file.path}
            onSelect={selectFile}
            ariaLabel="Workspace files"
            renderItem={(file) => (
              <>
                <strong>{file.name}</strong>
                <small>{file.path}</small>
              </>
            )}
          />
        ) : (
          <p>No files in this view.</p>
        )}
      </aside>
      {selected && loaded ? (
        <DetailPane
          title={selected.name}
          description={selected.path}
          actions={
            <>
              <button
                type="button"
                disabled={saving}
                onClick={() => api.openWorkspacePath(selected.path, true)}
              >
                Reveal
              </button>
              <button
                type="button"
                disabled={saving || draft === loaded.content}
                aria-label="Save file"
                onClick={saveFile}
              >
                {saving ? 'Saving...' : 'Save'}
              </button>
            </>
          }
          inspector={
            <>
              <h2>World links</h2>
              {links.status === 'loading' ? (
                <p>Loading world links...</p>
              ) : links.status === 'error' ? (
                <p>World links unavailable: {links.error}</p>
              ) : linked.length ? (
                linked.map((link) => (
                  <p key={`${link.target_type}:${link.target_id}`}>
                    {link.target_type}: {link.target_id}
                  </p>
                ))
              ) : (
                <p>No world links.</p>
              )}
            </>
          }
          inspectorLabel="File details"
        >
          {error && (
            <div role="alert" className={styles.error}>
              {error}
            </div>
          )}
          {clipboardError && (
            <div role="alert" className={styles.error}>
              {clipboardError}
            </div>
          )}
          {conflict && (
            <div className={styles.conflict}>
              <button
                type="button"
                aria-label="Reload file"
                onClick={() => selectFile(selected.path)}
              >
                Reload
              </button>
              <button type="button" aria-label="Copy local draft" onClick={copyDraft}>
                Copy
              </button>
            </div>
          )}
          <textarea
            aria-label="File content"
            value={draft}
            onChange={(event) => setDraft(event.target.value)}
          />
        </DetailPane>
      ) : (
        <PageState
          isEmpty
          emptyTitle={selected ? 'Loading file content' : 'Select a file'}
          emptyDescription="File content is loaded only when you select it."
        />
      )}
    </div>
  );
}
