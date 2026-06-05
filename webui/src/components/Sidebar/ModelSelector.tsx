import { useAppState } from '../../AppState';

export default function ModelSelector() {
  const { state, dispatch } = useAppState();
  const models = state.metadata?.models ?? [];

  return (
    <div className="sb-section">
      <div className="sb-label">Model</div>
      <select
        className="sb-select"
        value={state.selectedModel}
        onChange={(e) => dispatch({ type: 'SET_MODEL', model: e.target.value })}
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
