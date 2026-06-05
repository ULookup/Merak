import { useState } from 'react';
import styles from './Cells.module.css';

interface Props {
  toolName: string;
  toolArgs?: string;
  toolOutput?: string;
  toolRunning?: boolean;
  toolIsError?: boolean;
}

export default function ToolCell({
  toolName,
  toolArgs,
  toolOutput,
  toolRunning,
  toolIsError,
}: Props) {
  const [expanded, setExpanded] = useState(false);
  return (
    <div className={`${styles.tool} ${toolIsError ? styles.toolError : ''}`}>
      <div
        className={styles.toolHeader}
        onClick={() => setExpanded(!expanded)}
        style={{ cursor: 'pointer' }}
        data-testid="tool-header"
      >
        <span
          className={`${styles.toolIndicator} ${toolRunning ? styles.toolIndicatorRunning : styles.toolIndicatorDone}`}
        />
        <strong>{toolName}</strong>
        <span style={{ color: '#555768', fontSize: 12 }}>
          {toolRunning ? 'running...' : 'done'}
        </span>
      </div>
      {expanded && toolArgs && (
        <div className={styles.toolMeta}>
          <strong>Args:</strong>
          <pre>{toolArgs}</pre>
        </div>
      )}
      {expanded && toolOutput && (
        <div className={styles.toolMeta}>
          <strong>Output:</strong>
          <pre>{toolOutput}</pre>
        </div>
      )}
    </div>
  );
}
