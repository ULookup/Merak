import { useEffect, useId, useRef, useState, type ReactNode } from 'react';
import { useModalFocusTrap } from '../../hooks/useModalFocusTrap';
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
  const inspectorId = useId();
  const inspectorToggleRef = useRef<HTMLButtonElement>(null);
  const inspectorRef = useRef<HTMLElement>(null);
  const [compact, setCompact] = useState(false);
  const [inspectorOpen, setInspectorOpen] = useState(true);

  function closeInspector(restoreFocus = false) {
    setInspectorOpen(false);
    if (restoreFocus) inspectorToggleRef.current?.focus();
  }

  useEffect(() => {
    const query = window.matchMedia?.('(max-width: 1180px)');
    if (!query) return;
    const update = () => {
      setCompact(query.matches);
      setInspectorOpen(!query.matches);
    };
    update();
    query.addEventListener?.('change', update);
    return () => query.removeEventListener?.('change', update);
  }, []);
  useModalFocusTrap(inspectorRef, compact && inspectorOpen, () => closeInspector(true));

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
        {actions || inspector ? (
          <div className={styles.actions}>
            {inspector ? (
              <button
                ref={inspectorToggleRef}
                type="button"
                className={styles.inspectorToggle}
                aria-controls={inspectorId}
                aria-expanded={inspectorOpen}
                aria-label={`${inspectorOpen ? 'Hide' : 'Show'} ${inspectorLabel}`}
                onClick={() => setInspectorOpen((open) => !open)}
              >
                {inspectorOpen ? 'Hide details' : 'Show details'}
              </button>
            ) : null}
            {actions}
          </div>
        ) : null}
      </header>
      <div className={styles.body}>
        <div className={styles.content} inert={compact && inspectorOpen}>
          {children}
        </div>
        {inspectorOpen ? (
          <button
            type="button"
            className={styles.backdrop}
            aria-label={`Close ${inspectorLabel}`}
            onClick={() => closeInspector(true)}
          />
        ) : null}
        {inspector ? (
          <aside
            ref={inspectorRef}
            id={inspectorId}
            className={styles.inspector}
            aria-label={inspectorLabel}
            data-open={inspectorOpen}
            aria-hidden={!inspectorOpen}
            role={compact && inspectorOpen ? 'dialog' : undefined}
            aria-modal={compact && inspectorOpen ? true : undefined}
            onKeyDown={(event) => {
              if (event.key === 'Escape') closeInspector(true);
            }}
          >
            <button
              type="button"
              className={styles.inspectorClose}
              aria-label={`Close ${inspectorLabel}`}
              onClick={() => closeInspector(true)}
            >
              Close
            </button>
            <div>{inspector}</div>
          </aside>
        ) : null}
      </div>
    </section>
  );
}
