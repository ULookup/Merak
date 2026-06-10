import { useAppState } from '../../AppState';
import styles from './WorkflowMonitor.module.css';

export default function WorkflowMonitor() {
  const { state } = useAppState();
  const conditions = state.pipelineConditions ?? [];
  const history = state.pipelineHistory ?? [];
  const metCount = conditions.filter(c => c.met).length;
  const total = conditions.length;
  const pct = total > 0 ? Math.round((metCount / total) * 100) : 0;

  return (
    <div className={styles.container}>
      <div className={styles.title}>工作流状态</div>

      {total > 0 && (
        <div className={styles.section}>
          <div className={styles.progressHeader}>
            <span>条件进度</span>
            <span>{metCount}/{total} ({pct}%)</span>
          </div>
          <div className={styles.progressBar}>
            <div className={styles.progressFill} style={{ width: `${pct}%` }} />
          </div>
          <div className={styles.conditionList}>
            {conditions.map((c, i) => (
              <div key={i} className={styles.conditionItem}>
                <span className={c.met ? styles.checkGreen : styles.checkGray}>
                  {c.met ? '✓' : '○'}
                </span>
                <span className={styles.conditionName}>{c.name}</span>
                {c.current !== undefined && (
                  <span className={styles.conditionProgress}>
                    {c.current}/{c.target}
                  </span>
                )}
              </div>
            ))}
          </div>
        </div>
      )}

      {history.length > 0 && (
        <div className={styles.section}>
          <div className={styles.sectionTitle}>最近转换</div>
          {history.slice(0, 5).map((h) => (
            <div key={h.id} className={styles.historyItem}>
              <span className={styles.historyPhases}>
                {h.from} → {h.to}
              </span>
              <span className={styles.historyTrigger}>{h.trigger}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
