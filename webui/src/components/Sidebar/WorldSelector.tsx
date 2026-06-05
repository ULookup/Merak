import { useAppState } from '../../AppState';

export default function WorldSelector() {
  const { state, dispatch } = useAppState();

  return (
    <div className="sb-section">
      <div className="sb-label">World</div>
      <select
        className="sb-select"
        value={state.worldId ?? ''}
        onChange={(e) => dispatch({ type: 'SET_WORLD', worldId: e.target.value || null })}
      >
        <option value="">None</option>
      </select>
    </div>
  );
}
