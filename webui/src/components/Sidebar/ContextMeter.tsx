import { useAppState } from '../../AppState';

export default function ContextMeter() {
  const { state } = useAppState();
  const model = state.metadata?.models?.find((m) => m.name === state.selectedModel);
  const budget = model?.max_context_tokens ?? 128000;
  const used = state.usage.inputTokens + state.usage.outputTokens;
  const pct = Math.min(100, Math.round((used / budget) * 100));

  return (
    <div className="ctx-meter">
      Context<span className="ctx-pct">{pct}%</span>
      <div className="ctx-bar">
        <div className="ctx-fill" style={{ width: `${pct}%` }} />
      </div>
      {(used / 1000).toFixed(1)}K / {(budget / 1000).toFixed(0)}K tokens
    </div>
  );
}
