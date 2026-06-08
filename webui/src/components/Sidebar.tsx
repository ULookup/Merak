import { X } from 'lucide-react';
import BrandMark from './BrandMark';
import styles from './Sidebar.module.css';
import ContextMeter from './Sidebar/ContextMeter';
import ModelSelector from './Sidebar/ModelSelector';
import SessionList from './Sidebar/SessionList';
import SettingsPanel from './Sidebar/SettingsPanel';
import ToolPanel from './Sidebar/ToolPanel';
import WorldSelector from './Sidebar/WorldSelector';
import PipelineNavigator from './Sidebar/PipelineNavigator';
import SkillBrowser from './Sidebar/SkillBrowser';

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
        <X size={17} aria-hidden="true" strokeWidth={2.4} />
      </button>
      <div className={styles.brandSection}>
        <BrandMark />
      </div>
      <WorldSelector />
      <PipelineNavigator />
      <SessionList />
      <ModelSelector />
      <SkillBrowser />
      <ToolPanel />
      <ContextMeter />
      <SettingsPanel />
    </aside>
  );
}
