import { useAppState } from '../../AppState';
import styles from '../Sidebar.module.css';

export default function ModelSelector() {
  const { state, dispatch } = useAppState();
  const models = state.metadata?.models ?? [];

  return (
    <div className={styles.section}>
      <div className={styles.label}>Model</div>
      <select
        className={styles.select}
        value={state.selectedModel}
        onChange={(e) => dispatch({ type: 'SET_MODEL', model: e.target.value })}
        data-testid="model-select"
      >
        {models.map((m) => (
          <option key={m.name} value={m.name}>
            {m.name}
          </option>
        ))}
      </select>
    </div>
  );
}
