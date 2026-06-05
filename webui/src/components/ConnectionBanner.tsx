import type { ConnectionState } from '../hooks/useSSE';

interface Props {
  state: ConnectionState;
}

const styles: Record<ConnectionState, { bg: string; border: string; color: string }> = {
  connecting:   { bg: '#0f1a2e', border: '#1e3a5f', color: '#60a5fa' },
  connected:    { bg: 'transparent', border: 'transparent', color: 'transparent' },
  reconnecting: { bg: '#2a1f0e', border: '#5a4a1e', color: '#f59e0b' },
  disconnected: { bg: '#1a0f0f', border: '#4a2020', color: '#fca5a5' },
};

const labels: Record<ConnectionState, string> = {
  connecting:   'Connecting...',
  connected:    '',
  reconnecting: 'Connection lost. Reconnecting...',
  disconnected: 'Unable to connect. Check if the server is running.',
};

export default function ConnectionBanner({ state }: Props) {
  if (state === 'connected') return null;

  const s = styles[state];

  return (
    <div
      role="status"
      aria-live="polite"
      style={{
        background: s.bg,
        borderBottom: `1px solid ${s.border}`,
        color: s.color,
        fontSize: 12,
        padding: '6px 16px',
        textAlign: 'center',
      }}
      data-testid="connection-banner"
    >
      {labels[state]}
    </div>
  );
}
