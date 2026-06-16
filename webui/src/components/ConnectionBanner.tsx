import { isDesktopApp } from '../desktop';
import type { ConnectionState } from '../hooks/useSSE';
import { useI18n } from '../i18n';
import styles from './ConnectionBanner.module.css';

interface Props {
  state: ConnectionState;
}

export default function ConnectionBanner({ state }: Props) {
  const { t } = useI18n();

  if (state === 'connected') return null;

  const desktop = isDesktopApp();
  const label = t(`connection.${desktop ? 'desktop.' : ''}${state}`);

  return (
    <div
      role="status"
      aria-live="polite"
      className={`${styles.banner} ${styles[state]} ${desktop ? styles.desktop : ''}`}
      data-testid="connection-banner"
    >
      <span className={styles.message}>{label}</span>
      {desktop && <span className={styles.meta}>{t('connection.desktop')}</span>}
    </div>
  );
}
