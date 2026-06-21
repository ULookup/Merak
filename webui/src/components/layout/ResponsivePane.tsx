import { useEffect, useId, useRef, useState, type ReactNode } from 'react';
import { useModalFocusTrap } from '../../hooks/useModalFocusTrap';
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

  useModalFocusTrap(paneRef, compact && open, close);

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
            close();
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
