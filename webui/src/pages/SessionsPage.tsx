import { useState } from 'react';
import { useAppState } from '../AppState';
import ConnectionBanner from '../components/ConnectionBanner';
import InspectorPanel from '../components/InspectorPanel';
import MainPanel from '../components/MainPanel';
import SessionList from '../components/Sidebar/SessionList';
import type { ConnectionState } from '../hooks/useSSE';
import styles from './SessionsPage.module.css';

interface SessionsPageProps {
  connectionState?: ConnectionState;
  onOpenGuide?: () => void;
}

export default function SessionsPage({
  connectionState = 'connecting',
  onOpenGuide,
}: SessionsPageProps) {
  const { state } = useAppState();
  const [historyOpen, setHistoryOpen] = useState(() => window.innerWidth >= 768);
  const [inspectorOpen, setInspectorOpen] = useState(() => window.innerWidth >= 1180);
  const selectedSession = state.sessions.find((session) => session.id === state.sessionId);
  const conversationTitle = selectedSession?.title || 'New conversation';

  return (
    <div className={styles.page} aria-label="Sessions workbench">
      <section
        className={`${styles.history} ${historyOpen ? styles.historyOpen : ''}`}
        aria-label="Session history"
      >
        <header className={styles.historyHeader}>
          <div>
            <span>Workspace</span>
            <h2>Conversation history</h2>
          </div>
        </header>
        <SessionList worldId={state.worldId ?? undefined} />
      </section>

      <section className={styles.conversation} aria-label="Conversation workspace">
        <ConnectionBanner state={connectionState} />
        <MainPanel
          title={conversationTitle}
          connectionState={connectionState}
          historyOpen={historyOpen}
          inspectorOpen={inspectorOpen}
          onToggleHistory={() => setHistoryOpen((open) => !open)}
          onToggleInspector={() => setInspectorOpen((open) => !open)}
          onOpenGuide={onOpenGuide}
        />
      </section>

      <InspectorPanel open={inspectorOpen} onClose={() => setInspectorOpen(false)} />
    </div>
  );
}
