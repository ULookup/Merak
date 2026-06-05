import { useAppState } from '../../AppState';
import styles from '../Sidebar.module.css';

export default function WorldSelector() {
  const { state, dispatch } = useAppState();

  return (
    <div className={styles.section}>
      <div className={styles.label}>World</div>
      <select
        className={styles.select}
        value={state.worldId ?? ''}
        onChange={(e) => dispatch({ type: 'SET_WORLD', worldId: e.target.value || null })}
        data-testid="world-select"
        aria-label="Select world"
      >
        <option value="">None</option>
      </select>
    </div>
  );
}
