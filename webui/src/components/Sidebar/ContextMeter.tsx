import { useAppState } from '../../AppState';
import styles from './ContextMeter.module.css';

export default function ContextMeter() {
  const { state } = useAppState();
  const model = state.metadata?.models?.find((m) => m.name === state.selectedModel);
  const budget = model?.max_context_tokens ?? 128000;
  const used = state.usage.inputTokens + state.usage.outputTokens;
  const pct = Math.min(100, Math.round((used / budget) * 100));

  return (
    <div className={styles.meter}>
      Context<span className={styles.pct}>{pct}%</span>
      <div className={styles.bar}>
        <div className={styles.fill} style={{ width: `${pct}%` }} />
      </div>
      {(used / 1000).toFixed(1)}K / {(budget / 1000).toFixed(0)}K tokens
    </div>
  );
}
