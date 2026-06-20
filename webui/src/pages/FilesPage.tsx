import { useEffect, useMemo, useRef, useState } from 'react';
import { api, formatApiError } from '../api/client';
import { filesApi } from '../api/files';
import type { WorkspaceFile, WorkspaceFileContent, WorldFileLinkRecord } from '../api/types';
import DetailPane from '../components/layout/DetailPane';
import PageState from '../components/layout/PageState';
import ResourceList from '../components/layout/ResourceList';
import styles from './FilesPage.module.css';

type Props = { worldId: string };

function isConflict(error: unknown) {
  const value = error as { status?: number; code?: string };
  return value?.status === 409 || value?.code === 'file_conflict';
}

export default function FilesPage({ worldId }: Props) {
  const [files, setFiles] = useState<WorkspaceFile[]>([]);
  const [links, setLinks] = useState<WorldFileLinkRecord[]>([]);
  const [selectedPath, setSelectedPath] = useState<string | null>(null);
  const [loaded, setLoaded] = useState<WorkspaceFileContent | null>(null);
  const [draft, setDraft] = useState('');
  const [query, setQuery] = useState('');
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const [conflict, setConflict] = useState(false);
  const generation = useRef(0);

  const loadList = () => {
    const token = ++generation.current;
    setLoading(true);
    setError(null);
    setSelectedPath(null);
    setLoaded(null);
    Promise.all([
      api.listWorkspaceFiles({ world_id: worldId }),
      filesApi.listWorldFiles(worldId).catch(() => ({ ok: false, items: [] })),
    ])
      .then(([result, linked]) => {
        if (token !== generation.current) return;
        setFiles(result.files);
        setLinks(linked.items ?? ('files' in linked ? (linked.files ?? []) : []));
      })
      .catch(
        (reason) =>
          token === generation.current && setError(formatApiError(reason, 'Could not load files.')),
      )
      .finally(() => token === generation.current && setLoading(false));
  };

  useEffect(() => {
    setSaving(false);
    setConflict(false);
    loadList();
    return () => {
      generation.current += 1;
    };
  }, [worldId]);

  const visible = useMemo(() => {
    const needle = query.trim().toLowerCase();
    return files.filter(
      (file) =>
        !needle ||
        file.name.toLowerCase().includes(needle) ||
        file.path.toLowerCase().includes(needle),
    );
  }, [files, query]);

  async function selectFile(path: string) {
    if (saving) return;
    const token = ++generation.current;
    setSelectedPath(path);
    setLoaded(null);
    setConflict(false);
    setError(null);
    try {
      const result = await api.readWorkspaceFile(path);
      if (token !== generation.current) return;
      setLoaded(result.file);
      setDraft(result.file.content);
    } catch (reason) {
      if (token === generation.current) setError(formatApiError(reason, 'Could not read file.'));
    }
  }

  async function saveFile() {
    if (!loaded || saving) return;
    const target = loaded.path;
    const token = generation.current;
    setSaving(true);
    setError(null);
    setConflict(false);
    try {
      const result = await api.saveWorkspaceFile(target, draft, loaded.version);
      if (token !== generation.current || selectedPath !== target) return;
      setLoaded({
        ...loaded,
        content: draft,
        version: result.file.version,
        updated_at: result.file.updated_at,
      });
    } catch (reason) {
      if (token !== generation.current || selectedPath !== target) return;
      setConflict(isConflict(reason));
      setError(formatApiError(reason, 'Save failed.'));
    } finally {
      if (token === generation.current) setSaving(false);
    }
  }

  const selected = files.find((file) => file.path === selectedPath) ?? null;
  const linked = selected ? links.filter((link) => link.file_path === selected.path) : [];

  if (loading) return <PageState loading loadingLabel="Loading files" />;
  if (error && files.length === 0) return <PageState error={new Error(error)} onRetry={loadList} />;
  if (files.length === 0)
    return (
      <PageState
        isEmpty
        emptyTitle="No workspace files yet"
        emptyDescription="Files created by Merak will appear here."
      />
    );

  return (
    <div className={styles.page}>
      <aside className={styles.library} aria-label="File library">
        <header>
          <h1>Files</h1>
          <button type="button" onClick={loadList}>
            Refresh
          </button>
        </header>
        <input
          aria-label="Search files"
          placeholder="Search files"
          value={query}
          onChange={(event) => setQuery(event.target.value)}
        />
        <ResourceList
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
      </aside>
      {selected && loaded ? (
        <DetailPane
          title={selected.name}
          description={selected.path}
          actions={
            <>
              <button type="button" onClick={() => api.openWorkspacePath(selected.path, true)}>
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
              {linked.length ? (
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
          {conflict && (
            <div className={styles.conflict}>
              <button
                type="button"
                aria-label="Reload file"
                onClick={() => selectFile(selected.path)}
              >
                Reload
              </button>
              <button
                type="button"
                aria-label="Copy local draft"
                onClick={() => navigator.clipboard?.writeText(draft)}
              >
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
