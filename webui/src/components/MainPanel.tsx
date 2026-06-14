import { CircleHelp, Menu, PanelRight } from 'lucide-react';
import type { ConnectionState } from '../hooks/useSSE';
import { useAppState } from '../AppState';
import { LanguageToggle, useI18n } from '../i18n';
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
  const { t } = useI18n();

  const currentAgent = state.agents.find((a) => a.id === state.agentId);
  const currentWorld = state.worlds.find((w) => w.id === state.worldId);

  const headerTitle = currentAgent
    ? `${currentAgent.display_name || currentAgent.name} · ${currentWorld?.name ?? ''}`
    : `Merak ${t('app.workbench')}`;

  const headerSubtitle = currentAgent
    ? (() => {
        const kind = currentAgent.kind;
        if (kind === 'god' || kind === '0') return t('agent.god');
        if (kind && (kind.includes('manager') || (kind >= '1' && kind <= '4'))) {
          return t('agent.manager');
        }
        return t('agent.character');
      })()
    : t('composer.placeholder');

  const activeScene = state.storyOverview?.current_scene ?? null;

  return (
    <main className={styles.main} role="main" aria-label="Chat">
      <header className={styles.header}>
        <button
          className={styles.iconBtn}
          onClick={onToggleSidebar}
          aria-label={sidebarOpen ? t('app.closeSidebar') : t('app.openSidebar')}
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
                · {t('composer.scene')}: {activeScene.title ?? activeScene.id}
              </span>
            )}
          </div>
        </div>
        <div className={styles.headerSpacer} />
        <div
          className={`${styles.liveChip} ${connectionState === 'connected' ? styles.liveChipOn : ''}`}
        >
          <span className={styles.liveDot} />
          {connectionState === 'connected' ? t('app.runtimeReady') : t('app.connecting')}
        </div>
        <div className={styles.languageToggle}>
          <LanguageToggle />
        </div>
        <button
          className={styles.iconBtn}
          onClick={onOpenGuide}
          aria-label={t('app.openGuide')}
          title={t('app.openGuide')}
        >
          <CircleHelp size={18} aria-hidden="true" strokeWidth={2.2} />
        </button>
        <button
          className={styles.iconBtn}
          onClick={onToggleInspector}
          aria-label={inspectorOpen ? t('app.closeInspector') : t('app.openInspector')}
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
