import BrandMark from './BrandMark';
import styles from './Sidebar.module.css';
import ContextMeter from './Sidebar/ContextMeter';
import ModelSelector from './Sidebar/ModelSelector';
import SessionList from './Sidebar/SessionList';
import ToolPanel from './Sidebar/ToolPanel';
import WorldSelector from './Sidebar/WorldSelector';

interface SidebarProps {
  open?: boolean;
  onClose?: () => void;
}

export default function Sidebar({ open = true, onClose }: SidebarProps) {
  return (
    <aside
      className={`${styles.sidebar} ${open ? styles.sidebarOpen : ''}`}
      role="navigation"
      aria-label="Sidebar"
    >
      {open && <div className={styles.overlay} onClick={onClose} data-testid="sidebar-overlay" />}
      <button
        className={styles.closeBtn}
        onClick={onClose}
        aria-label="Close sidebar"
        data-testid="close-sidebar-btn"
      >
        &#10005;
      </button>
      <div className={styles.brandSection}>
        <BrandMark />
      </div>
      <WorldSelector />
      <SessionList />
      <ModelSelector />
      <ToolPanel />
      <ContextMeter />
    </aside>
  );
}
