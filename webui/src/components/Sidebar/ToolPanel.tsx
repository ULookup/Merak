import { useAppState } from '../../AppState';
import styles from '../Sidebar.module.css';
import tpStyles from './ToolPanel.module.css';

export default function ToolPanel() {
  const { state } = useAppState();
  const tools = state.metadata?.tools ?? [];

  return (
    <div className={styles.section}>
      <div className={styles.label}>Tools ({tools.length})</div>
      <div className={tpStyles.panel}>
        {tools.map((t) => (
          <div key={t.name} className={tpStyles.row} data-testid="tool-row">
            <div className={tpStyles.rowName}>
              <span
                className={`${tpStyles.icon} ${t.source === 'mcp' ? tpStyles.iconAsk : tpStyles.iconSafe}`}
              />
              {t.name}
            </div>
            <span
              className={`${tpStyles.badge} ${t.source === 'mcp' ? tpStyles.badgeAsk : tpStyles.badgeSafe}`}
            >
              {t.source}
            </span>
          </div>
        ))}
      </div>
    </div>
  );
}
