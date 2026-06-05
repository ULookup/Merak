import { useAppState } from '../../AppState';
import { api } from '../../api/client';

export default function SessionList() {
  const { state, dispatch } = useAppState();

  async function select(id: string) {
    dispatch({ type: 'SET_SESSION', sessionId: id });
    try {
      const data = await api.events(id, 0);
      for (const ev of data.events ?? []) {
        dispatch({ type: 'APPLY_SSE', frame: ev });
      }
    } catch { /* ignore */ }
  }

  async function create() {
    try {
      const data = await api.createSession();
      const id = data.session_id;
      dispatch({ type: 'SET_SESSIONS', sessions: [
        ...state.sessions,
        { id, title: '', last_seq: 0, created_at: '', updated_at: '', archived_at: null },
      ]});
      select(id);
    } catch { /* ignore */ }
  }

  return (
    <div className="sb-section sb-sessions">
      <div className="sb-label">Sessions</div>
      <div className="sb-session-list">
        {state.sessions.map((s) => (
          <div
            key={s.id}
            className={`sb-session-item ${s.id === state.sessionId ? 'active' : ''}`}
            onClick={() => select(s.id)}
          >
            {s.title || s.id.slice(0, 12)}
          </div>
        ))}
      </div>
      <button className="sb-new-btn" onClick={create}>New Session</button>
    </div>
  );
}
