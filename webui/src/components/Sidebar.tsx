import { useState } from 'react';
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
import WorkflowMonitor from './Sidebar/WorkflowMonitor';
import { useAppState } from '../AppState';

interface SidebarProps {
  open?: boolean;
  onClose?: () => void;
}

export default function Sidebar({ open = true, onClose }: SidebarProps) {
  const { state, dispatch } = useAppState();
  const [showSettings, setShowSettings] = useState(false);

  function phaseLabel(phase: string): string {
    const map: Record<string, string> = {
      direction_selection: '创作方向',
      worldbuilding: '世界观搭建',
      character_creation: '角色创建',
      plot_architecture: '故事规划',
      scene_writing: '章节写作',
      reflection: '回顾反思',
    };
    return map[phase] ?? phase;
  }

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
      <WorkflowMonitor />
      {state.worldId && (
        <div className={styles.section}>
          <h3 className={styles.sectionTitle}>创作进度</h3>
          {state.pipelinePhase && (
            <div className={styles.phaseLabel}>
              当前阶段：{phaseLabel(state.pipelinePhase)}
            </div>
          )}
          <button className={styles.navItem} onClick={() => dispatch({ type: 'SET_INSPECTOR_TAB', tab: 'story' })}>
            世界观与角色
          </button>
          <button className={styles.navItem} onClick={() => dispatch({ type: 'SET_INSPECTOR_TAB', tab: 'files' })}>
            作品文件
          </button>
        </div>
      )}
      <SessionList />
      <ModelSelector />
      <ToolPanel />
      <ContextMeter />
      <div className={styles.bottomSection}>
        <button className={styles.navItem} onClick={() => setShowSettings((v) => !v)}>
          设置
        </button>
      </div>
      {showSettings && <SettingsPanel />}
    </aside>
  );
}
