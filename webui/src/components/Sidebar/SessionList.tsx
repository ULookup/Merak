import { useState } from 'react';
import { Archive, Pencil, Plus, RotateCcw, Sparkles } from 'lucide-react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import { useToast } from '../Toast';
import styles from './SessionList.module.css';
import type { SessionSummary } from '../../api/types';

interface SessionListProps {
  worldId?: string;
  agentId?: string;
}

function turnLabel(count: number) {
  return `${count} ${count === 1 ? 'turn' : 'turns'}`;
}

function formatSessionTime(value: string | null | undefined) {
  if (!value) return 'Not started';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleString(undefined, {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  });
}

export default function SessionList({ worldId, agentId }: SessionListProps) {
  const { state, dispatch } = useAppState();
  const { showToast } = useToast();
  const [editingId, setEditingId] = useState<string | null>(null);
  const [editValue, setEditValue] = useState('');

  async function create() {
    try {
      const data = await api.createSession('', worldId, agentId);
      const id = data.session_id;
      dispatch({
        type: 'SET_SESSIONS',
        sessions: [
          ...state.sessions,
          { id, title: '', world_id: worldId ?? null, agent_id: agentId ?? null, last_seq: 0, created_at: '', updated_at: '', archived_at: null },
        ],
      });
      select(id);
    } catch {
      showToast('Failed to create session', 'error');
    }
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
      try {
        const data = await api.updateSession(id, newTitle);
        const updated = data.session;
        dispatch({
          type: 'SET_SESSIONS',
          sessions: state.sessions.map((s) =>
            s.id === id ? { ...s, title: updated.title, updated_at: updated.updated_at } : s,
          ),
        });
      } catch {
        showToast('Failed to rename session', 'error');
      }
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

  const sessions = state.sessions.filter((s) => {
    if (worldId && s.world_id !== worldId) return false;
    if (agentId && s.agent_id !== agentId) return false;
    return true;
  }).sort(
    (a, b) =>
      new Date(b.updated_at || b.created_at).getTime() -
      new Date(a.updated_at || a.created_at).getTime(),
  );
  const activeSessions = sessions.filter((session) => !session.archived_at);
  const archivedSessions = sessions.filter((session) => session.archived_at);

  function renderSession(session: SessionSummary) {
    const title = session.title || 'New Session';
    const metadata = `${turnLabel(session.last_seq)} / ${formatSessionTime(session.updated_at || session.created_at)}`;
    const label = `Session ${title}${session.archived_at ? ', archived' : ''}, ${turnLabel(session.last_seq)}`;

    return (
      <li
        key={session.id}
        className={`${styles.item} ${session.id === state.sessionId ? styles.itemActive : ''} ${
          session.archived_at ? styles.itemArchived : ''
        }`}
        onClick={() => select(session.id)}
        onContextMenu={(e) => {
          e.preventDefault();
          startRename(session);
        }}
        aria-label={label}
      >
        {editingId === session.id ? (
          <input
            className={styles.renameInput}
            value={editValue}
            onChange={(e) => setEditValue(e.target.value)}
            onBlur={() => confirmRename(session.id)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') confirmRename(session.id);
              if (e.key === 'Escape') cancelRename();
            }}
            onClick={(e) => e.stopPropagation()}
            autoFocus
          />
        ) : (
          <>
            <span className={styles.sessionMain}>
              <span
                className={styles.title}
                onDoubleClick={(e) => {
                  e.stopPropagation();
                  startRename(session);
                }}
              >
                {title}
              </span>
              <span className={styles.meta}>{metadata}</span>
            </span>
            {session.id === state.sessionId && (
              <span className={styles.actions}>
                <button
                  className={styles.generateBtn}
                  aria-label="Rename session"
                  onClick={(e) => {
                    e.stopPropagation();
                    startRename(session);
                  }}
                >
                  <Pencil size={14} aria-hidden="true" strokeWidth={2.2} />
                </button>
                <button
                  className={styles.generateBtn}
                  aria-label="Generate title"
                  onClick={(e) => {
                    e.stopPropagation();
                    generateTitle(session.id);
                  }}
                >
                  <Sparkles size={14} aria-hidden="true" strokeWidth={2.2} />
                </button>
                <button
                  className={styles.generateBtn}
                  aria-label={session.archived_at ? 'Restore session' : 'Archive session'}
                  onClick={(e) => {
                    e.stopPropagation();
                    archiveSession(session, !session.archived_at);
                  }}
                >
                  {session.archived_at ? (
                    <RotateCcw size={14} aria-hidden="true" strokeWidth={2.2} />
                  ) : (
                    <Archive size={14} aria-hidden="true" strokeWidth={2.2} />
                  )}
                </button>
              </span>
            )}
            {session.archived_at && <span className={styles.badge}>Archived</span>}
          </>
        )}
      </li>
    );
  }

  return (
    <div className={styles.list}>
      <div className={styles.header}>
        <span>Sessions</span>
        <button className={styles.newBtn} onClick={create} aria-label="New session">
          <Plus size={15} aria-hidden="true" strokeWidth={2.4} />
        </button>
      </div>
      <div className={styles.groups}>
        <section>
          <div className={styles.groupTitle}>
            <span>Active Sessions</span>
            <strong>{activeSessions.length}</strong>
          </div>
          <ul>
            {activeSessions.length > 0 ? (
              activeSessions.map(renderSession)
            ) : (
              <li className={styles.empty}>No active sessions yet.</li>
            )}
          </ul>
        </section>
        {archivedSessions.length > 0 && (
          <section>
            <div className={styles.groupTitle}>
              <span>Archived Sessions</span>
              <strong>{archivedSessions.length}</strong>
            </div>
            <ul>{archivedSessions.map(renderSession)}</ul>
          </section>
        )}
      </div>
    </div>
  );
}
