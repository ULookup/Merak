import { BookOpen, CheckCircle2, Loader2, ScrollText, Users, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import type { EndSceneResponse } from '../../api/types';
import { useAppState } from '../../AppState';
import styles from './EndSceneModal.module.css';

interface Props {
  worldId: string;
  sceneId: string;
  sceneTitle: string;
  chapterId: string;
  onClose: () => void;
}

export default function EndSceneModal({ worldId, sceneId, sceneTitle, chapterId, onClose }: Props) {
  const { state, dispatch } = useAppState();
  const [finalMarkdown, setFinalMarkdown] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [result, setResult] = useState<EndSceneResponse | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    function onKey(e: KeyboardEvent) {
      if (e.key === 'Escape' && !submitting) onClose();
    }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose, submitting]);

  async function handleEnd() {
    setSubmitting(true);
    setError(null);
    try {
      const res = await api.endScene(worldId, sceneId, {
        final_markdown: finalMarkdown || undefined,
        session_id: state.sessionId,
      });
      setResult(res);
      dispatch({ type: 'SET_STORY_VERSION' });
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setSubmitting(false);
    }
  }

  if (result) {
    return (
      <div className={styles.scrim} role="presentation">
        <section className={styles.modal} role="dialog" aria-modal="true" aria-label="Scene ended">
          <button className={styles.closeBtn} onClick={onClose} aria-label="Close">
            <X size={17} aria-hidden="true" strokeWidth={2.4} />
          </button>
          <div className={styles.resultIcon}>
            <CheckCircle2 size={28} aria-hidden="true" strokeWidth={2} />
          </div>
          <h2>Scene Concluded</h2>
          <p className={styles.resultSub}>"{sceneTitle}" has been marked complete.</p>

          <div className={styles.resultGrid}>
            <div className={styles.resultCard}>
              <ScrollText size={16} aria-hidden="true" />
              <strong>{result.diary_count}</strong>
              <span>diaries written</span>
            </div>
            <div className={styles.resultCard}>
              <Users size={16} aria-hidden="true" />
              <strong>{result.relations_updated}</strong>
              <span>relations updated</span>
            </div>
            <div className={styles.resultCard}>
              <BookOpen size={16} aria-hidden="true" />
              <strong>{result.proposed_foreshadowing.length}</strong>
              <span>foreshadowing hints</span>
            </div>
          </div>

          {result.proposed_foreshadowing.length > 0 && (
            <div className={styles.foreshadowList}>
              <div className={styles.listTitle}>Suggested Foreshadowing</div>
              {result.proposed_foreshadowing.map((f) => (
                <div key={f.id} className={styles.foreshadowItem}>{f.content}</div>
              ))}
            </div>
          )}

          {result.leak_risks > 0 && (
            <div className={styles.leakNotice}>
              {result.leak_risks} secret leak risk{result.leak_risks > 1 ? 's' : ''} detected. Review knowledge boundaries.
            </div>
          )}

          <button className={styles.doneBtn} onClick={onClose}>
            Return to workbench
          </button>
        </section>
      </div>
    );
  }

  return (
    <div className={styles.scrim} role="presentation">
      <section className={styles.modal} role="dialog" aria-modal="true" aria-label="End scene">
        <button className={styles.closeBtn} onClick={onClose} aria-label="Cancel" disabled={submitting}>
          <X size={17} aria-hidden="true" strokeWidth={2.4} />
        </button>
        <div className={styles.iconWrap}>
          <ScrollText size={28} aria-hidden="true" strokeWidth={2.1} />
        </div>
        <div className={styles.kicker}>End Scene</div>
        <h2>{sceneTitle}</h2>
        <p>Ending this scene will write character diaries, update relationships, and suggest new foreshadowing threads.</p>

        <label className={styles.field}>
          <span>Final text (optional)</span>
          <textarea
            className={styles.textarea}
            value={finalMarkdown}
            onChange={(e) => setFinalMarkdown(e.target.value)}
            placeholder="Paste the scene's final markdown..."
            rows={8}
          />
        </label>

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.secondary} onClick={onClose} disabled={submitting}>
            Cancel
          </button>
          <button className={styles.primary} onClick={handleEnd} disabled={submitting}>
            {submitting ? (
              <>
                <Loader2 size={15} aria-hidden="true" className={styles.spin} />
                Ending scene...
              </>
            ) : (
              'End Scene'
            )}
          </button>
        </div>
      </section>
    </div>
  );
}
