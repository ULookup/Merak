import type { ConnectionState } from '../hooks/useSSE';
import BrandMark from './BrandMark';
import ChatTimeline from './ChatTimeline';
import Composer from './Composer';
import styles from './MainPanel.module.css';

interface MainPanelProps {
  onToggleSidebar?: () => void;
  onToggleInspector?: () => void;
  sidebarOpen?: boolean;
  inspectorOpen?: boolean;
  connectionState?: ConnectionState;
}

export default function MainPanel({
  onToggleSidebar,
  onToggleInspector,
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
          <span aria-hidden="true">☰</span>
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
          onClick={onToggleInspector}
          aria-label={inspectorOpen ? 'Close inspector' : 'Open inspector'}
          data-testid="inspector-btn"
        >
          <span aria-hidden="true">◫</span>
        </button>
      </header>
      <ChatTimeline />
      <Composer />
    </main>
  );
}
