import { useState } from 'react';
import { api, formatApiError } from '../api/client';
import type { PendingCreation } from '../api/types';
import styles from './AskUserPrompt.module.css';

interface Props {
  request: PendingCreation;
  onResolved: () => void;
}

export default function CreationRequestDialog({ request, onResolved }: Props) {
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [modifications, setModifications] = useState(
    request.preview ? JSON.stringify(request.preview, null, 2) : '',
  );

  async function resolve(decision: 'allow' | 'deny') {
    if (submitting) return;
    setSubmitting(true);
    setError(null);
    try {
      let parsedModifications: Record<string, unknown> | undefined;
      if (decision === 'allow' && modifications.trim()) {
        const parsed = JSON.parse(modifications) as unknown;
        if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
          throw new Error('Modifications must be a JSON object.');
        }
        parsedModifications = parsed as Record<string, unknown>;
      }
      await api.resolveCreation(request.id, decision, parsedModifications);
      onResolved();
    } catch (cause) {
      setError(formatApiError(cause, 'Could not resolve the creation request.'));
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <div
      className={styles.overlay}
      role="dialog"
      aria-modal="true"
      aria-labelledby="creation-title"
    >
      <div className={styles.card}>
        <h2 id="creation-title">Creation request</h2>
        <p>
          The agent wants to run <strong>{request.toolName}</strong>.
        </p>
        {request.preview && (
          <>
            <label className={styles.label} htmlFor="creation-modifications">
              Proposed values
            </label>
            <textarea
              id="creation-modifications"
              className={styles.input}
              value={modifications}
              onChange={(event) => setModifications(event.target.value)}
              disabled={submitting}
            />
          </>
        )}
        {error && (
          <p className={styles.error} role="alert">
            {error}
          </p>
        )}
        <div className={styles.actions}>
          <button
            className={styles.choice}
            type="button"
            disabled={submitting}
            onClick={() => resolve('deny')}
          >
            Deny
          </button>
          <button
            className={styles.submit}
            type="button"
            disabled={submitting}
            onClick={() => resolve('allow')}
          >
            {submitting ? 'Submitting...' : 'Allow'}
          </button>
        </div>
      </div>
    </div>
  );
}
