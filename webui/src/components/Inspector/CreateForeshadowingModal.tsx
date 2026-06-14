import { Eye, Lightbulb, Loader2, Tag, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from './CreateModal.module.css';

interface Props {
  worldId: string;
  onClose: () => void;
  onCreated?: () => void;
}

export default function CreateForeshadowingModal({ worldId, onClose, onCreated }: Props) {
  const { state } = useAppState();
  const [content, setContent] = useState('');
  const [payOffIdea, setPayOffIdea] = useState('');
  const [hintLevel, setHintLevel] = useState('visible');
  const [tags, setTags] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    function onKey(e: KeyboardEvent) { if (e.key === 'Escape' && !submitting) onClose(); }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose, submitting]);

  async function handleSubmit() {
    if (!content.trim()) return;
    setSubmitting(true);
    setError(null);
    try {
      await api.createForeshadowing(worldId, {
        content: content.trim(),
        pay_off_idea: payOffIdea.trim() || undefined,
        hint_level: hintLevel,
        tags: tags ? tags.split(',').map(t => t.trim()).filter(Boolean) : undefined,
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
      <section className={styles.modal} role="dialog" aria-modal="true" aria-label="Plant foreshadowing">
        <button className={styles.closeBtn} onClick={onClose} aria-label="Cancel" disabled={submitting}>
          <X size={17} aria-hidden="true" strokeWidth={2.4} />
        </button>
        <div className={styles.iconWrap}>
          <Lightbulb size={28} aria-hidden="true" strokeWidth={2.1} />
        </div>
        <div className={styles.kicker}>Plant Thread</div>
        <h2>New Foreshadowing</h2>
        <p>Drop a clue that will pay off later. Good foreshadowing feels inevitable in hindsight.</p>

        <label className={styles.field}>
          <span>Content</span>
          <textarea
            className={styles.textarea}
            value={content}
            onChange={(e) => setContent(e.target.value)}
            placeholder="What hint or clue is being planted..."
            rows={4}
          />
        </label>

        <label className={styles.field}>
          <span>Pay-off idea</span>
          <input
            className={styles.input}
            value={payOffIdea}
            onChange={(e) => setPayOffIdea(e.target.value)}
            placeholder="How this might resolve..."
          />
        </label>

        <div className={styles.row}>
          <label className={styles.field}>
            <span><Eye size={12} aria-hidden="true" /> Hint level</span>
            <select className={styles.select} value={hintLevel} onChange={(e) => setHintLevel(e.target.value)}>
              <option value="visible">Visible</option>
              <option value="subtle">Subtle</option>
              <option value="obvious">Obvious</option>
            </select>
          </label>
          <label className={styles.field}>
            <span><Tag size={12} aria-hidden="true" /> Tags</span>
            <input
              className={styles.input}
              value={tags}
              onChange={(e) => setTags(e.target.value)}
              placeholder="mystery, character..."
            />
          </label>
        </div>

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.secondary} onClick={onClose} disabled={submitting}>Cancel</button>
          <button className={styles.primary} onClick={handleSubmit} disabled={submitting || !content.trim()}>
            {submitting ? <><Loader2 size={15} aria-hidden="true" className={styles.spin} /> Planting...</> : 'Plant Thread'}
          </button>
        </div>
      </section>
    </div>
  );
}
