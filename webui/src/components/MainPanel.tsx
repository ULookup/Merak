import { CircleHelp, Menu, PanelRight } from 'lucide-react';
import type { ConnectionState } from '../hooks/useSSE';
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
          <div className={styles.title}>Merak Workbench</div>
          <div className={styles.subtitle}>Worldbuilding agent runtime</div>
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
