import { useState } from 'react';
import { Archive, Pencil, Plus, RotateCcw, Sparkles } from 'lucide-react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import { useToast } from '../Toast';
import styles from './SessionList.module.css';

export default function SessionList() {
  const { state, dispatch } = useAppState();
  const { showToast } = useToast();
  const [editingId, setEditingId] = useState<string | null>(null);
  const [editValue, setEditValue] = useState('');

  async function create() {
    const data = await api.createSession();
    const id = data.session_id;
    dispatch({
      type: 'SET_SESSIONS',
      sessions: [
        ...state.sessions,
        { id, title: '', world_id: null, agent_id: null, last_seq: 0, created_at: '', updated_at: '', archived_at: null },
      ],
    });
    select(id);
  }

  function select(id: string) {
    dispatch({ type: 'SET_SESSION', sessionId: id });
  }

  function startRename(session: { id: string; title: string }) {
    setEditingId(session.id);
    setEditValue(session.title || '');
  }

  function cancelRename() {
    setEditingId(null);
    setEditValue('');
  }

  async function confirmRename(id: string) {
    const newTitle = editValue.trim();
    if (newTitle) {
      const data = await api.updateSession(id, newTitle);
      const updated = data.session;
      dispatch({
        type: 'SET_SESSIONS',
        sessions: state.sessions.map((s) =>
          s.id === id ? { ...s, title: updated.title, updated_at: updated.updated_at } : s,
        ),
      });
    }
    setEditingId(null);
    setEditValue('');
  }

  async function generateTitle(id: string) {
    try {
      const res = await api.generateTitle(id);
      const title = res.title;
      if (title) {
        const data = await api.updateSession(id, title);
        const updated = data.session;
        dispatch({
          type: 'SET_SESSIONS',
          sessions: state.sessions.map((s) =>
            s.id === id ? { ...s, title: updated.title, updated_at: updated.updated_at } : s,
          ),
        });
      }
    } catch {
      startRename(state.sessions.find((s) => s.id === id) || { id, title: '' });
    }
  }

  async function archiveSession(session: (typeof state.sessions)[number], archived: boolean) {
    try {
      const res = await api.archiveSession(session, archived);
      dispatch({
        type: 'SET_SESSIONS',
        sessions: state.sessions.map((s) => (s.id === session.id ? res.session : s)),
      });
      showToast(archived ? 'Session archived.' : 'Session restored.', res.fallback ? 'info' : 'success');
    } catch (error) {
      showToast(error instanceof Error ? error.message : 'Could not update session.', 'error');
    }
  }

  const sessions = [...state.sessions].sort(
    (a, b) =>
      new Date(b.updated_at || b.created_at).getTime() -
      new Date(a.updated_at || a.created_at).getTime(),
  );

  return (
    <div className={styles.list}>
      <div className={styles.header}>
        <span>Sessions</span>
        <button className={styles.newBtn} onClick={create} aria-label="New session">
          <Plus size={15} aria-hidden="true" strokeWidth={2.4} />
        </button>
      </div>
      <ul>
        {sessions.map((s) => (
          <li
            key={s.id}
            className={`${styles.item} ${s.id === state.sessionId ? styles.itemActive : ''}`}
            onClick={() => select(s.id)}
            onContextMenu={(e) => {
              e.preventDefault();
              startRename(s);
            }}
          >
            {editingId === s.id ? (
              <input
                className={styles.renameInput}
                value={editValue}
                onChange={(e) => setEditValue(e.target.value)}
                onBlur={() => confirmRename(s.id)}
                onKeyDown={(e) => {
                  if (e.key === 'Enter') confirmRename(s.id);
                  if (e.key === 'Escape') cancelRename();
                }}
                onClick={(e) => e.stopPropagation()}
                autoFocus
              />
            ) : (
              <>
                <span
                  className={styles.title}
                  onDoubleClick={(e) => {
                    e.stopPropagation();
                    startRename(s);
                  }}
                  aria-label={s.title || 'New Session'}
                >
                  {s.title || 'New Session'}
                </span>
                {s.id === state.sessionId && (
                  <span className={styles.actions}>
                    <button
                      className={styles.generateBtn}
                      aria-label="Rename session"
                      onClick={(e) => {
                        e.stopPropagation();
                        startRename(s);
                      }}
                    >
                      <Pencil size={14} aria-hidden="true" strokeWidth={2.2} />
                    </button>
                    <button
                      className={styles.generateBtn}
                      aria-label="Generate title"
                      onClick={(e) => {
                        e.stopPropagation();
                        generateTitle(s.id);
                      }}
                    >
                      <Sparkles size={14} aria-hidden="true" strokeWidth={2.2} />
                    </button>
                    <button
                      className={styles.generateBtn}
                      aria-label={s.archived_at ? 'Restore session' : 'Archive session'}
                      onClick={(e) => {
                        e.stopPropagation();
                        archiveSession(s, !s.archived_at);
                      }}
                    >
                      {s.archived_at ? (
                        <RotateCcw size={14} aria-hidden="true" strokeWidth={2.2} />
                      ) : (
                        <Archive size={14} aria-hidden="true" strokeWidth={2.2} />
                      )}
                    </button>
                  </span>
                )}
                {s.archived_at && <span className={styles.badge}>Archived</span>}
              </>
            )}
          </li>
        ))}
      </ul>
    </div>
  );
}
