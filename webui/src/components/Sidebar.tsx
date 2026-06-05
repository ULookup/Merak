import ContextMeter from './Sidebar/ContextMeter';
import ModelSelector from './Sidebar/ModelSelector';
import SessionList from './Sidebar/SessionList';
import ToolPanel from './Sidebar/ToolPanel';
import WorldSelector from './Sidebar/WorldSelector';
import styles from './Sidebar.module.css';

export default function Sidebar() {
  return (
    <aside className={styles.sidebar} role="navigation" aria-label="Sidebar">
      <WorldSelector />
      <SessionList />
      <ModelSelector />
      <ToolPanel />
      <ContextMeter />
    </aside>
  );
}
