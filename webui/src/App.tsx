import { useEffect } from 'react';
import { api } from './api/client';
import { AppStateProvider, useAppState } from './AppState';
import ErrorBoundary from './components/ErrorBoundary';
import MainPanel from './components/MainPanel';
import Sidebar from './components/Sidebar';
import { useSSE } from './hooks/useSSE';
import styles from './App.module.css';

function AppInner() {
  const { state, dispatch } = useAppState();

  useEffect(() => {
    api
      .metadata()
      .then((meta) => {
        dispatch({ type: 'SET_METADATA', metadata: meta });
      })
      .catch(() => {});
    api
      .listSessions()
      .then((data) => {
        dispatch({ type: 'SET_SESSIONS', sessions: data.sessions ?? [] });
      })
      .catch(() => {});
  }, [dispatch]);

  useEffect(() => {
    if (!state.sessionId) {
      api
        .createSession()
        .then((data) => {
          const id = data.session_id as string;
          dispatch({ type: 'SET_SESSION', sessionId: id });
        })
        .catch(() => {});
      return;
    }
    api
      .listSessions()
      .then((data) => {
        dispatch({ type: 'SET_SESSIONS', sessions: data.sessions ?? [] });
      })
      .catch(() => {});
  }, [state.sessionId, dispatch]);

  const sseUrl = state.sessionId ? api.sseUrl(state.sessionId, state.lastSeq) : null;

  useSSE(sseUrl, dispatch, state.lastSeq);

  return (
    <div className={styles.layout}>
      <ErrorBoundary>
        <Sidebar />
      </ErrorBoundary>
      <ErrorBoundary>
        <MainPanel />
      </ErrorBoundary>
    </div>
  );
}

export default function App() {
  return (
    <AppStateProvider>
      <AppInner />
    </AppStateProvider>
  );
}
