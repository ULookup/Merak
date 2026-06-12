import { isDesktopApp } from '../desktop';
import type { ConnectionState } from '../hooks/useSSE';
import styles from './ConnectionBanner.module.css';

interface Props {
  state: ConnectionState;
}

const labels: Record<ConnectionState, string> = {
  connecting: 'Connecting...',
  connected: '',
  reconnecting: 'Connection lost. Reconnecting...',
  disconnected: 'Unable to connect. Check if the server is running.',
};

const desktopLabels: Record<ConnectionState, string> = {
  connecting: 'Connecting to the local Merak runtime...',
  connected: '',
  reconnecting: 'Runtime connection was interrupted. Reconnecting...',
  disconnected: 'Merak runtime is not reachable. Start the runtime service, then return here.',
};

export default function ConnectionBanner({ state }: Props) {
  if (state === 'connected') return null;

  const desktop = isDesktopApp();
  const label = desktop ? desktopLabels[state] : labels[state];

  return (
    <div
      role="status"
      aria-live="polite"
      className={`${styles.banner} ${styles[state]} ${desktop ? styles.desktop : ''}`}
      data-testid="connection-banner"
    >
      <span className={styles.message}>{label}</span>
      {desktop && <span className={styles.meta}>Desktop</span>}
    </div>
  );
}
