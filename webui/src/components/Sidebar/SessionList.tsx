import { useState } from 'react';
import { Plus, Sparkles } from 'lucide-react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import './SessionList.css';

export default function SessionList() {
  const { state, dispatch } = useAppState();
  const [editingId, setEditingId] = useState<string | null>(null);
  const [editValue, setEditValue] = useState('');

  async function create() {
    const data = await api.createSession();
    const id = data.session_id;
    dispatch({
      type: 'SET_SESSIONS',
      sessions: [
        ...state.sessions,
        { id, title: '', last_seq: 0, created_at: '', updated_at: '', archived_at: null },
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

  const sessions = [...state.sessions].sort(
    (a, b) =>
      new Date(b.updated_at || b.created_at).getTime() -
      new Date(a.updated_at || a.created_at).getTime(),
  );

  return (
    <div className="session-list">
      <div className="session-list-header">
        <span>Sessions</span>
        <button className="session-new-btn" onClick={create} aria-label="New session">
          <Plus size={15} aria-hidden="true" strokeWidth={2.4} />
        </button>
      </div>
      <ul>
        {sessions.map((s) => (
          <li
            key={s.id}
            className={s.id === state.sessionId ? 'active' : ''}
            onClick={() => select(s.id)}
            onContextMenu={(e) => {
              e.preventDefault();
              startRename(s);
            }}
          >
            {editingId === s.id ? (
              <input
                className="session-rename-input"
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
                  className="session-title"
                  onDoubleClick={(e) => {
                    e.stopPropagation();
                    startRename(s);
                  }}
                  aria-label={s.title || 'New Session'}
                >
                  {s.title || 'New Session'}
                </span>
                {s.id === state.sessionId && (
                  <button
                    className="session-generate-btn"
                    aria-label="Generate title"
                    onClick={(e) => {
                      e.stopPropagation();
                      generateTitle(s.id);
                    }}
                  >
                    <Sparkles size={14} aria-hidden="true" strokeWidth={2.2} />
                  </button>
                )}
              </>
            )}
          </li>
        ))}
      </ul>
    </div>
  );
}
