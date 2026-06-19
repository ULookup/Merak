import { FormEvent, useState } from 'react';
import { api, formatApiError } from '../api/client';
import type { PendingAsk } from '../api/types';
import styles from './AskUserPrompt.module.css';

interface Props {
  request: PendingAsk;
  onResolved: () => void;
}

export default function AskUserPrompt({ request, onResolved }: Props) {
  const [response, setResponse] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function submit(event: FormEvent) {
    event.preventDefault();
    const answer = response.trim();
    if (!answer || submitting) return;
    setSubmitting(true);
    setError(null);
    try {
      await api.respondToAsk(request.runId, answer, request.callId);
      onResolved();
    } catch (cause) {
      setError(formatApiError(cause, 'Could not send your response.'));
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <div
      className={styles.overlay}
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
                className={response === choice ? styles.selected : styles.choice}
                onClick={() => setResponse(choice)}
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
          autoFocus
        />
        {error && (
          <p className={styles.error} role="alert">
            {error}
          </p>
        )}
        <div className={styles.actions}>
          <button className={styles.submit} type="submit" disabled={submitting || !response.trim()}>
            {submitting ? 'Sending...' : 'Send response'}
          </button>
        </div>
      </form>
    </div>
  );
}
