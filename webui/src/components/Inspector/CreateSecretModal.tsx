import { KeyRound, Loader2, ShieldAlert, Users, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from './CreateModal.module.css';

interface Props {
  worldId: string;
  onClose: () => void;
  onCreated?: () => void;
}

export default function CreateSecretModal({ worldId, onClose, onCreated }: Props) {
  const { state } = useAppState();
  const [title, setTitle] = useState('');
  const [truth, setTruth] = useState('');
  const [publicVersion, setPublicVersion] = useState('');
  const [stakes, setStakes] = useState('');
  const [awareIds, setAwareIds] = useState('');
  const [suspiciousIds, setSuspiciousIds] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    function onKey(e: KeyboardEvent) { if (e.key === 'Escape' && !submitting) onClose(); }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose, submitting]);

  async function handleSubmit() {
    if (!title.trim() && !truth.trim()) return;
    setSubmitting(true);
    setError(null);
    try {
      await api.createSecret(worldId, {
        title: title.trim() || undefined,
        truth: truth.trim() || undefined,
        public_version: publicVersion.trim() || undefined,
        stakes: stakes.trim() || undefined,
        aware_character_ids: awareIds ? awareIds.split(',').map(t => t.trim()).filter(Boolean) : undefined,
        suspicious_character_ids: suspiciousIds ? suspiciousIds.split(',').map(t => t.trim()).filter(Boolean) : undefined,
        session_id: state.sessionId,
      });
      onCreated?.();
      onClose();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <div className={styles.scrim} role="presentation">
      <section className={styles.modal} role="dialog" aria-modal="true" aria-label="Create secret">
        <button className={styles.closeBtn} onClick={onClose} aria-label="Cancel" disabled={submitting}>
          <X size={17} aria-hidden="true" strokeWidth={2.4} />
        </button>
        <div className={styles.iconWrap}>
          <KeyRound size={28} aria-hidden="true" strokeWidth={2.1} />
        </div>
        <div className={styles.kicker}>Knowledge Boundary</div>
        <h2>New Secret</h2>
        <p>Define what some characters know and others don't. Secrets create dramatic tension and believable character blind spots.</p>

        <label className={styles.field}>
          <span>Title</span>
          <input className={styles.input} value={title} onChange={(e) => setTitle(e.target.value)} placeholder="e.g. The true parentage of..." />
        </label>

        <label className={styles.field}>
          <span>Truth</span>
          <textarea className={styles.textarea} value={truth} onChange={(e) => setTruth(e.target.value)} placeholder="What actually happened..." rows={3} />
        </label>

        <label className={styles.field}>
          <span>Public version</span>
          <input className={styles.input} value={publicVersion} onChange={(e) => setPublicVersion(e.target.value)} placeholder="What most characters believe..." />
        </label>

        <label className={styles.field}>
          <span>Stakes</span>
          <input className={styles.input} value={stakes} onChange={(e) => setStakes(e.target.value)} placeholder="What happens if this comes out..." />
        </label>

        <div className={styles.row}>
          <label className={styles.field}>
            <span><Users size={12} aria-hidden="true" /> Aware characters</span>
            <input className={styles.input} value={awareIds} onChange={(e) => setAwareIds(e.target.value)} placeholder="agent IDs, comma-separated" />
          </label>
          <label className={styles.field}>
            <span><ShieldAlert size={12} aria-hidden="true" /> Suspicious</span>
            <input className={styles.input} value={suspiciousIds} onChange={(e) => setSuspiciousIds(e.target.value)} placeholder="agent IDs, comma-separated" />
          </label>
        </div>

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.secondary} onClick={onClose} disabled={submitting}>Cancel</button>
          <button className={styles.primary} onClick={handleSubmit} disabled={submitting || (!title.trim() && !truth.trim())}>
            {submitting ? <><Loader2 size={15} aria-hidden="true" className={styles.spin} /> Creating...</> : 'Create Secret'}
          </button>
        </div>
      </section>
    </div>
  );
}
