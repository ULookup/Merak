import { useState } from 'react';
import { CircleHelp, Menu, PanelRight, X } from 'lucide-react';
import { useAppState } from '../AppState';
import type { ConnectionState } from '../hooks/useSSE';
import BrandMark from './BrandMark';
import ChatTimeline from './ChatTimeline';
import Composer from './Composer';
import styles from './MainPanel.module.css';

interface MainPanelProps {
  title?: string;
  onToggleHistory?: () => void;
  onToggleSidebar?: () => void;
  onToggleInspector?: () => void;
  onOpenGuide?: () => void;
  historyOpen?: boolean;
  sidebarOpen?: boolean;
  inspectorOpen?: boolean;
  connectionState?: ConnectionState;
}

export default function MainPanel({
  title,
  onToggleHistory,
  onToggleSidebar,
  onToggleInspector,
  onOpenGuide,
  historyOpen,
  sidebarOpen,
  inspectorOpen,
  connectionState = 'connecting',
}: MainPanelProps) {
  const { state } = useAppState();
  const [guideOpen, setGuideOpen] = useState(false);

  const currentAgent = state.agents.find((a) => a.id === state.agentId);
  const currentWorld = state.worlds.find((w) => w.id === state.worldId);

  const headerTitle =
    title ||
    (currentAgent
      ? `${currentAgent.display_name || currentAgent.name} / ${currentWorld?.name ?? ''}`
      : 'Merak Workbench');

  const headerSubtitle = currentAgent
    ? (() => {
        const kind = currentAgent.kind;
        if (kind === 'god' || kind === '0') return 'Omniscient / 20 tools';
        if (kind && (kind.includes('manager') || (kind >= '1' && kind <= '4'))) {
          return 'Manager / 1 tool';
        }
        return 'Character / 3 tools';
      })()
    : 'Worldbuilding agent runtime';

  const activeScene = state.storyOverview?.current_scene ?? null;

  return (
    <main className={styles.main} role="main" aria-label="Conversation">
      <header className={styles.header}>
        <button
          className={styles.iconBtn}
          onClick={onToggleHistory ?? onToggleSidebar}
          aria-label={
            onToggleHistory
              ? historyOpen
                ? 'Close session history'
                : 'Open session history'
              : sidebarOpen
                ? 'Close sidebar'
                : 'Open sidebar'
          }
          aria-controls="session-history-panel"
          aria-expanded={onToggleHistory ? historyOpen : sidebarOpen}
          data-testid="menu-btn"
        >
          <Menu size={18} aria-hidden="true" strokeWidth={2.2} />
        </button>
        <div className={styles.mobileBrand}>
          <BrandMark compact />
        </div>
        <div>
          <h1 className={styles.title}>{headerTitle}</h1>
          <div className={styles.subtitle}>
            {headerSubtitle}
            {activeScene && (
              <span className={styles.activeSceneBadge}>
                / Scene: {activeScene.title ?? activeScene.id}
              </span>
            )}
          </div>
        </div>
        <div className={styles.headerSpacer} />
        <div
          className={`${styles.liveChip} ${connectionState === 'connected' ? styles.liveChipOn : ''}`}
        >
          <span className={styles.liveDot} />
          {connectionState === 'connected' ? 'Live SSE' : connectionState}
        </div>
        <button
          className={styles.iconBtn}
          onClick={() => {
            onOpenGuide?.();
            setGuideOpen(true);
          }}
          aria-label="Open workbench guide"
          title="Open workbench guide"
        >
          <CircleHelp size={18} aria-hidden="true" strokeWidth={2.2} />
        </button>
        <button
          className={styles.iconBtn}
          onClick={onToggleInspector}
          aria-label={inspectorOpen ? 'Close inspector' : 'Open inspector'}
          aria-controls="session-inspector-panel"
          aria-expanded={inspectorOpen}
          data-testid="inspector-btn"
        >
          <PanelRight size={18} aria-hidden="true" strokeWidth={2.2} />
        </button>
      </header>

      {guideOpen && (
        <div className={styles.guideOverlay} role="presentation">
          <section
            className={styles.guide}
            role="dialog"
            aria-modal="false"
            aria-labelledby="workbench-guide-title"
          >
            <div className={styles.guideHeader}>
              <div>
                <h2 id="workbench-guide-title">Workbench guide</h2>
                <p>Keep the creative loop moving without losing context.</p>
              </div>
              <button
                type="button"
                className={styles.iconBtn}
                onClick={() => setGuideOpen(false)}
                aria-label="Close workbench guide"
              >
                <X size={18} aria-hidden="true" strokeWidth={2.2} />
              </button>
            </div>
            <div className={styles.guideGrid}>
              <article>
                <strong>Start with context</strong>
                <span>Choose the world, agent lane, and scene before sending a prompt.</span>
              </article>
              <article>
                <strong>Watch the run</strong>
                <span>Use the timeline, live status, and inspector tabs to follow tool work.</span>
              </article>
              <article>
                <strong>Capture output</strong>
                <span>Generated files appear in Files, where drafts can be opened and edited.</span>
              </article>
              <article>
                <strong>Close safely</strong>
                <span>
                  Active runs and unsaved editor changes are protected before window close.
                </span>
              </article>
            </div>
          </section>
        </div>
      )}

      <ChatTimeline connectionState={connectionState} />
      <Composer />
    </main>
  );
}
