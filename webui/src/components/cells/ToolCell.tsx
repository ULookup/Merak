import { useState } from 'react';

interface Props {
  toolName: string;
  toolArgs?: string;
  toolOutput?: string;
  toolRunning?: boolean;
  toolIsError?: boolean;
}

export default function ToolCell({ toolName, toolArgs, toolOutput, toolRunning, toolIsError }: Props) {
  const [expanded, setExpanded] = useState(false);
  return (
    <div className={`msg msg-tool ${toolIsError ? 'tool-error' : ''}`}>
      <div className="th" onClick={() => setExpanded(!expanded)} style={{ cursor: 'pointer' }}>
        <span className={`tool-indicator ${toolRunning ? 'running' : 'done'}`} />
        <strong>{toolName}</strong>
        <span style={{ color: '#555768', fontSize: 12 }}>{toolRunning ? 'running...' : 'done'}</span>
      </div>
      {expanded && toolArgs && (
        <div className="tool-meta">
          <strong>Args:</strong>
          <pre>{toolArgs}</pre>
        </div>
      )}
      {expanded && toolOutput && (
        <div className="tool-meta">
          <strong>Output:</strong>
          <pre>{toolOutput}</pre>
        </div>
      )}
    </div>
  );
}
