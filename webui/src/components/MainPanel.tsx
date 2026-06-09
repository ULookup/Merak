import { CircleHelp, Menu, PanelRight } from 'lucide-react';
import type { ConnectionState } from '../hooks/useSSE';
import { useAppState } from '../AppState';
import BrandMark from './BrandMark';
import ChatTimeline from './ChatTimeline';
import Composer from './Composer';
import styles from './MainPanel.module.css';

interface MainPanelProps {
  onToggleSidebar?: () => void;
  onToggleInspector?: () => void;
  onOpenGuide?: () => void;
  sidebarOpen?: boolean;
  inspectorOpen?: boolean;
  connectionState?: ConnectionState;
}

export default function MainPanel({
  onToggleSidebar,
  onToggleInspector,
  onOpenGuide,
  sidebarOpen,
  inspectorOpen,
  connectionState = 'connecting',
}: MainPanelProps) {
  const { state } = useAppState();

  const currentAgent = state.agents.find((a) => a.id === state.agentId);
  const currentWorld = state.worlds.find((w) => w.id === state.worldId);

  // Adapt agent kind to actual type (string, not number)
  const AGENT_ICONS: Record<string, string> = {
    'god': '\u{1F451}', 'map_manager': '\u{1F5FA}', 'history_manager': '\u{1F4DC}',
    'magic_system_manager': '\u{1F52E}', 'faction_manager': '\u{2694}', 'individual': '\u{1F9D1}', 'group': '\u{1F465}',
    '0': '\u{1F451}', '1': '\u{1F5FA}', '2': '\u{1F4DC}',
    '3': '\u{1F52E}', '4': '\u{2694}', '5': '\u{1F9D1}', '6': '\u{1F465}',
  };

  const headerTitle = currentAgent
    ? `${AGENT_ICONS[currentAgent.kind] ?? ''} ${currentAgent.display_name || currentAgent.name} · ${currentWorld?.name ?? ''}`
    : 'Merak Workbench';

  const headerSubtitle = currentAgent
    ? (() => {
        const kind = currentAgent.kind;
        if (kind === 'god' || kind === '0') return 'Omniscient · 20 tools';
        if (kind && (kind.includes('manager') || (kind >= '1' && kind <= '4'))) return 'Manager · 1 tool';
        return `Character · 3 tools`;
      })()
    : 'Worldbuilding agent runtime';

  // Find active scene
  const activeScene = state.storyOverview?.current_scene ?? null;

  return (
    <main className={styles.main} role="main" aria-label="Chat">
      <header className={styles.header}>
        <button
          className={styles.iconBtn}
          onClick={onToggleSidebar}
          aria-label={sidebarOpen ? 'Close sidebar' : 'Open sidebar'}
          data-testid="menu-btn"
        >
          <Menu size={18} aria-hidden="true" strokeWidth={2.2} />
        </button>
        <div className={styles.mobileBrand}>
          <BrandMark compact />
        </div>
        <div>
          <div className={styles.title}>{headerTitle}</div>
          <div className={styles.subtitle}>
            {headerSubtitle}
            {activeScene && (
              <span className={styles.activeSceneBadge}>
                · Scene: {activeScene.title ?? activeScene.id}
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
          onClick={onOpenGuide}
          aria-label="Open workbench guide"
          title="Open workbench guide"
        >
          <CircleHelp size={18} aria-hidden="true" strokeWidth={2.2} />
        </button>
        <button
          className={styles.iconBtn}
          onClick={onToggleInspector}
          aria-label={inspectorOpen ? 'Close inspector' : 'Open inspector'}
          data-testid="inspector-btn"
        >
          <PanelRight size={18} aria-hidden="true" strokeWidth={2.2} />
        </button>
      </header>
      <ChatTimeline connectionState={connectionState} />
      <Composer />
    </main>
  );
}
