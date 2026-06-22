import type { ReactNode } from 'react';
import styles from './PageState.module.css';

export type PageStateProps = {
  loading?: boolean;
  loadingLabel?: string;
  error?: Error | null;
  hasData?: boolean;
  isEmpty?: boolean;
  emptyTitle?: string;
  emptyDescription?: string;
  emptyAction?: ReactNode;
  onRetry?: () => void;
  children?: ReactNode;
};

export default function PageState({
  loading = false,
  loadingLabel = 'Loading',
  error = null,
  hasData = false,
  isEmpty = false,
  emptyTitle = 'Nothing here yet',
  emptyDescription,
  emptyAction,
  onRetry,
  children,
}: PageStateProps) {
  if (loading && !hasData) {
    return (
      <div className={styles.loading} role="status" aria-label={loadingLabel}>
        <span className={styles.skeleton} />
        <span className={styles.skeleton} />
        <span className={styles.skeletonShort} />
      </div>
    );
  }

  if (error && !hasData) {
    return (
      <div className={`${styles.state} ${styles.error}`} role="alert">
        <h2>Unable to load this content</h2>
        <p>{error.message}</p>
        {onRetry ? (
          <button type="button" onClick={onRetry}>
            Retry
          </button>
        ) : null}
      </div>
    );
  }

  if (isEmpty && !hasData) {
    return (
      <div className={styles.state}>
        <h2>{emptyTitle}</h2>
        {emptyDescription ? <p>{emptyDescription}</p> : null}
        {emptyAction}
      </div>
    );
  }

  return (
    <div className={styles.content}>
      {error ? (
        <div className={styles.warning} role="alert">
          <span>{error.message}</span>
          {onRetry ? (
            <button type="button" onClick={onRetry}>
              Retry
            </button>
          ) : null}
        </div>
      ) : null}
      {loading ? (
        <div className={styles.refreshing} role="status">
          {loadingLabel}
        </div>
      ) : null}
      {children}
    </div>
  );
}
