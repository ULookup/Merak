import ChatTimeline from './ChatTimeline';
import Composer from './Composer';
import styles from './MainPanel.module.css';

interface MainPanelProps {
  onToggleSidebar?: () => void;
  sidebarOpen?: boolean;
}

export default function MainPanel({ onToggleSidebar, sidebarOpen }: MainPanelProps) {
  return (
    <main className={styles.main} role="main" aria-label="Chat">
      <button
        className={styles.menuBtn}
        onClick={onToggleSidebar}
        aria-label={sidebarOpen ? 'Close sidebar' : 'Open sidebar'}
        data-testid="menu-btn"
      >
        &#9776;
      </button>
      <ChatTimeline />
      <Composer />
    </main>
  );
}
