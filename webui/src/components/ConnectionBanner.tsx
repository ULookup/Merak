import type { ConnectionState } from '../hooks/useSSE';

interface Props {
  state: ConnectionState;
}

const styles: Record<ConnectionState, { bg: string; border: string; color: string }> = {
  connecting: { bg: '#eef2ff', border: '#c7d2fe', color: '#4f46e5' },
  connected: { bg: 'transparent', border: 'transparent', color: 'transparent' },
  reconnecting: { bg: '#fff7ed', border: '#fed7aa', color: '#b7791f' },
  disconnected: { bg: '#fff1f2', border: '#fecaca', color: '#be123c' },
};

const labels: Record<ConnectionState, string> = {
  connecting: 'Connecting...',
  connected: '',
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
