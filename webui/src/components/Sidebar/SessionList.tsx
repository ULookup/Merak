import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from '../Sidebar.module.css';

export default function SessionList() {
  const { state, dispatch } = useAppState();

  async function select(id: string) {
    if (state.currentRun && !window.confirm('A run is in progress. Switch session anyway?')) {
      return;
    }
    dispatch({ type: 'SET_SESSION', sessionId: id });
    try {
      const data = await api.events(id, 0);
      for (const ev of data.events ?? []) {
        dispatch({ type: 'APPLY_SSE', frame: ev });
      }
    } catch {
      /* ignore */
    }
  }

  async function create() {
    try {
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
    } catch {
      /* ignore */
    }
  }

  return (
    <div className={`${styles.section} ${styles.sessions}`}>
      <div className={styles.label}>Sessions</div>
      <div className={styles.sessionList}>
        {state.sessions.length === 0 ? (
          <div
            style={{
              padding: '16px 12px',
              color: '#555768',
              fontSize: 12,
              textAlign: 'center',
              lineHeight: 1.5,
            }}
            data-testid="session-list-empty"
          >
            No sessions yet.
            <br />
            Create one to get started.
          </div>
        ) : (
          state.sessions.map((s) => (
            <div
              key={s.id}
              data-testid="session-item"
              className={`${styles.sessionItem} ${s.id === state.sessionId ? styles.sessionItemActive : ''}`}
              onClick={() => select(s.id)}
            >
              {s.title || s.id.slice(0, 12)}
            </div>
          ))
        )}
      </div>
      <button className={styles.newBtn} onClick={create} data-testid="new-session-btn">
        New Session
      </button>
    </div>
  );
}
