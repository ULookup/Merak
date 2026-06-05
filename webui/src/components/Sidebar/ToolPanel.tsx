import { useAppState } from '../../AppState';

export default function ToolPanel() {
  const { state } = useAppState();
  const tools = state.metadata?.tools ?? [];

  return (
    <div className="sb-section">
      <div className="sb-label">Tools</div>
      <div className="tool-panel">
        {tools.map((t) => (
          <div key={t.name} className="tool-row">
            <div className="tool-row-name">
              <span className={`tool-icon ${t.source === 'mcp' ? 'ask' : 'safe'}`} />
              {t.name}
            </div>
            <span className={`tool-badge badge-safe`}>{t.source}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
