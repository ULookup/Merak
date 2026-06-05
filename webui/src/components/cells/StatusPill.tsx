import type { StatusLabel } from '../../api/types';
import styles from './Cells.module.css';

const colors: Record<StatusLabel, string> = {
  idle: '#22c55e',
  thinking: '#a78bfa',
  acting: '#60a5fa',
  observing: '#94a3b8',
  waiting_approval: '#f59e0b',
};

interface Props {
  label: StatusLabel;
}

export default function StatusPill({ label }: Props) {
  return (
    <div className={styles.pill} style={{ color: colors[label], borderColor: colors[label] }}>
      {label === 'waiting_approval'
        ? 'Waiting Approval'
        : label.charAt(0).toUpperCase() + label.slice(1)}
    </div>
  );
}
