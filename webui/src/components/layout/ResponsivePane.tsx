import { useEffect, useId, useRef, useState, type ReactNode } from 'react';
import styles from './ResponsivePane.module.css';

type Props = {
  label: string;
  children: ReactNode;
  className?: string;
  side?: 'left' | 'right';
  breakpoint?: 980 | 1180;
  closeOnSelect?: boolean;
  inert?: boolean;
  'aria-hidden'?: boolean | 'true';
};

const focusable =
  'button:not([disabled]), [href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';

export default function ResponsivePane({
  label,
  children,
  className,
  side = 'left',
  breakpoint = 980,
  closeOnSelect = false,
  inert,
  'aria-hidden': ariaHidden,
}: Props) {
  const id = useId();
  const triggerRef = useRef<HTMLButtonElement>(null);
  const paneRef = useRef<HTMLElement>(null);
  const [compact, setCompact] = useState(false);
  const [open, setOpen] = useState(false);

  const close = (restoreFocus = true) => {
    setOpen(false);
    if (restoreFocus) triggerRef.current?.focus();
  };

  useEffect(() => {
    const query = window.matchMedia?.(`(max-width: ${breakpoint}px)`);
    if (!query) return;
    const update = () => {
      setCompact(query.matches);
      if (!query.matches) setOpen(false);
    };
    update();
    query.addEventListener?.('change', update);
    return () => query.removeEventListener?.('change', update);
  }, [breakpoint]);

  useEffect(() => {
    if (!compact || !open) return;
    paneRef.current?.querySelector<HTMLElement>(focusable)?.focus();
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        event.preventDefault();
        close();
        return;
      }
      if (event.key !== 'Tab') return;
      const nodes = [...(paneRef.current?.querySelectorAll<HTMLElement>(focusable) ?? [])];
      if (!nodes.length) return;
      const first = nodes[0];
      const last = nodes[nodes.length - 1];
      if (event.shiftKey && document.activeElement === first) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first.focus();
      }
    };
    document.addEventListener('keydown', onKeyDown);
    return () => document.removeEventListener('keydown', onKeyDown);
  }, [compact, open]);

  return (
    <>
      <button
        ref={triggerRef}
        type="button"
        className={styles.trigger}
        aria-controls={id}
        aria-expanded={open}
        onClick={() => setOpen(true)}
      >
        Open {label}
      </button>
      {open ? (
        <button
          type="button"
          className={styles.backdrop}
          aria-label={`Close ${label}`}
          onClick={() => close()}
        />
      ) : null}
      <aside
        ref={paneRef}
        id={id}
        className={[styles.pane, styles[side], className].filter(Boolean).join(' ')}
        data-open={open}
        role={compact && open ? 'dialog' : undefined}
        aria-modal={compact && open ? true : undefined}
        aria-label={label}
        aria-hidden={ariaHidden}
        inert={inert}
        onClickCapture={(event) => {
          if (closeOnSelect && compact && (event.target as Element).closest('[role="option"]'))
            close(false);
        }}
      >
        <button
          type="button"
          className={styles.close}
          aria-label={`Close ${label}`}
          onClick={() => close()}
        >
          Close
        </button>
        {children}
      </aside>
    </>
  );
}
