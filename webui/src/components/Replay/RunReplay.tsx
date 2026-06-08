import { useState, useMemo } from 'react';
import { useAppState } from '../../AppState';
import styles from './RunReplay.module.css';

export default function RunReplay() {
  const { state } = useAppState();
  const [expandedId, setExpandedId] = useState<string | null>(null);

  const timeline = useMemo(() => [...state.runTimeline].reverse(), [state.runTimeline]);

  if (timeline.length === 0) {
    return <p className={styles.empty}>暂无运行记录。开始一次运行后这里会显示步骤时间线。</p>;
  }

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <span className={styles.label}>运行回放</span>
        <span className={styles.badge}>{timeline.length} 步</span>
      </div>
      <div className={styles.timeline}>
        {timeline.map((item, i) => {
          const isExpanded = expandedId === item.id;
          const isLast = i === timeline.length - 1;
          return (
            <div key={item.id} className={styles.step}>
              <div className={styles.line}>
                <div className={`${styles.dot} ${styles[item.type]}`} />
                {!isLast && <div className={styles.connector} />}
              </div>
              <div className={styles.content}>
                <div
                  className={styles.stepHeader}
                  onClick={() => setExpandedId(isExpanded ? null : item.id)}
                >
                  <span className={styles.time}>
                    {new Date(item.at).toLocaleTimeString()}
                  </span>
                  <strong className={styles.stepLabel}>{item.label}</strong>
                  <span className={styles.chevron}>{isExpanded ? '▾' : '▸'}</span>
                </div>
                {isExpanded && item.detail && (
                  <pre className={styles.detail}>{item.detail}</pre>
                )}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
