import { FormEvent, RefObject, useEffect, useRef, useState } from 'react';
import { api, formatApiError } from '../api/client';
import type { PendingAsk } from '../api/types';
import styles from './AskUserPrompt.module.css';

interface Props {
  request: PendingAsk;
  onResolved: (callId: string) => void;
}

const focusableSelector =
  'button:not([disabled]), textarea:not([disabled]), input:not([disabled]), select:not([disabled]), [tabindex]:not([tabindex="-1"])';

export function useDialogFocus(dialogRef: RefObject<HTMLElement | null>) {
  useEffect(() => {
    const previousFocus =
      document.activeElement instanceof HTMLElement ? document.activeElement : null;
    const dialog = dialogRef.current;
    const focusables = () =>
      Array.from(dialog?.querySelectorAll<HTMLElement>(focusableSelector) ?? []);
    if (dialog && !dialog.contains(document.activeElement)) focusables()[0]?.focus();

    function trapFocus(event: KeyboardEvent) {
      if (event.key !== 'Tab' || !dialog) return;
      const elements = focusables();
      if (!elements.length) return;
      const first = elements[0];
      const last = elements[elements.length - 1];
      if (
        event.shiftKey &&
        (document.activeElement === first || !dialog.contains(document.activeElement))
      ) {
        event.preventDefault();
        last.focus();
      } else if (
        !event.shiftKey &&
        (document.activeElement === last || !dialog.contains(document.activeElement))
      ) {
        event.preventDefault();
        first.focus();
      }
    }

    dialog?.addEventListener('keydown', trapFocus);
    return () => {
      dialog?.removeEventListener('keydown', trapFocus);
      previousFocus?.focus();
    };
  }, [dialogRef]);
}

export default function AskUserPrompt({ request, onResolved }: Props) {
  const [response, setResponse] = useState('');
  const [selectedChoices, setSelectedChoices] = useState<string[]>([]);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const dialogRef = useRef<HTMLDivElement>(null);
  const requestIdRef = useRef(request.callId);
  requestIdRef.current = request.callId;
  useDialogFocus(dialogRef);

  useEffect(() => {
    setResponse('');
    setSelectedChoices([]);
    setSubmitting(false);
    setError(null);
  }, [request.callId]);

  const answer = response.trim() || selectedChoices.join(', ');

  function toggleChoice(choice: string) {
    if (request.multiSelect) {
      setSelectedChoices((current) =>
        current.includes(choice) ? current.filter((item) => item !== choice) : [...current, choice],
      );
      return;
    }
    setSelectedChoices([choice]);
    setResponse(choice);
  }

  async function submit(event: FormEvent) {
    event.preventDefault();
    if (!answer || submitting) return;
    setSubmitting(true);
    setError(null);
    const submittedCallId = request.callId;
    try {
      await api.respondToAsk(request.runId, request.callId, answer);
      onResolved(request.callId);
    } catch (cause) {
      if (requestIdRef.current === submittedCallId) {
        setError(formatApiError(cause, 'Could not send your response.'));
      }
    } finally {
      if (requestIdRef.current === submittedCallId) setSubmitting(false);
    }
  }

  return (
    <div
      className={styles.overlay}
      ref={dialogRef}
      role="dialog"
      aria-modal="true"
      aria-labelledby="ask-user-title"
    >
      <form className={styles.card} onSubmit={submit}>
        <h2 id="ask-user-title">Agent question</h2>
        <p className={styles.question}>{request.question}</p>
        {request.choices?.length ? (
          <div className={styles.choices}>
            {request.choices.map((choice) => (
              <button
                key={choice}
                type="button"
                className={selectedChoices.includes(choice) ? styles.selected : styles.choice}
                onClick={() => toggleChoice(choice)}
                aria-pressed={selectedChoices.includes(choice)}
                disabled={submitting}
              >
                {choice}
              </button>
            ))}
          </div>
        ) : null}
        <label className={styles.label} htmlFor="ask-user-response">
          Your response
        </label>
        <textarea
          id="ask-user-response"
          className={styles.input}
          value={response}
          onChange={(event) => setResponse(event.target.value)}
          disabled={submitting}
        />
        {error && (
          <p className={styles.error} role="alert">
            {error}
          </p>
        )}
        <div className={styles.actions}>
          <button className={styles.submit} type="submit" disabled={submitting || !answer}>
            {submitting ? 'Sending...' : 'Send response'}
          </button>
        </div>
      </form>
    </div>
  );
}
