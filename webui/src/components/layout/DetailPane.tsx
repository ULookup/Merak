import { useId, type ReactNode } from 'react';
import styles from './DetailPane.module.css';

export type DetailPaneProps = {
  title: ReactNode;
  description?: ReactNode;
  actions?: ReactNode;
  inspector?: ReactNode;
  inspectorLabel?: string;
  children: ReactNode;
  className?: string;
};

export default function DetailPane({
  title,
  description,
  actions,
  inspector,
  inspectorLabel = 'Related details',
  children,
  className,
}: DetailPaneProps) {
  const titleId = useId();

  return (
    <section
      className={[styles.pane, className].filter(Boolean).join(' ')}
      aria-labelledby={titleId}
    >
      <header className={styles.header}>
        <div className={styles.heading}>
          <h1 id={titleId}>{title}</h1>
          {description ? <div className={styles.description}>{description}</div> : null}
        </div>
        {actions ? <div className={styles.actions}>{actions}</div> : null}
      </header>
      <div className={styles.body}>
        <div className={styles.content}>{children}</div>
        {inspector ? (
          <aside className={styles.inspector} aria-label={inspectorLabel}>
            {inspector}
          </aside>
        ) : null}
      </div>
    </section>
  );
}
