import { Clapperboard, Loader2, MapPin, Users, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from './CreateModal.module.css';

interface Props {
  worldId: string;
  onClose: () => void;
  onCreated?: () => void;
}

export default function CreateSceneModal({ worldId, onClose, onCreated }: Props) {
  const { state } = useAppState();
  const overview = state.storyOverview;
  const [title, setTitle] = useState('');
  const [chapterId, setChapterId] = useState(overview?.current_chapter?.id ?? '');
  const [worldTime, setWorldTime] = useState('');
  const [participantIds, setParticipantIds] = useState('');
  const [narrative, setNarrative] = useState('');
  const [sectionId, setSectionId] = useState('');
  const [locationId, setLocationId] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    function onKey(e: KeyboardEvent) { if (e.key === 'Escape' && !submitting) onClose(); }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose, submitting]);

  async function handleSubmit() {
    if (!title.trim() || !chapterId.trim()) return;
    setSubmitting(true);
    setError(null);
    try {
      await api.createScene(worldId, {
        title: title.trim(),
        chapter_id: chapterId.trim(),
        world_time: worldTime.trim() || undefined,
        participant_ids: participantIds ? participantIds.split(',').map(t => t.trim()).filter(Boolean) : undefined,
        narrative: narrative.trim() || undefined,
        section_id: sectionId.trim() || undefined,
        location_id: locationId.trim() || undefined,
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
      <section className={styles.modal} role="dialog" aria-modal="true" aria-label="Create scene">
        <button className={styles.closeBtn} onClick={onClose} aria-label="Cancel" disabled={submitting}>
          <X size={17} aria-hidden="true" strokeWidth={2.4} />
        </button>
        <div className={styles.iconWrap}>
          <Clapperboard size={28} aria-hidden="true" strokeWidth={2.1} />
        </div>
        <div className={styles.kicker}>New Beat</div>
        <h2>Create Scene</h2>
        <p>Add a new scene to continue the narrative. Each scene is a contained story beat.</p>

        <label className={styles.field}>
          <span>Title</span>
          <input className={styles.input} value={title} onChange={(e) => setTitle(e.target.value)} placeholder="e.g. The Tavern Confrontation" />
        </label>

        <div className={styles.row}>
          <label className={styles.field}>
            <span>Chapter ID</span>
            <input className={styles.input} value={chapterId} onChange={(e) => setChapterId(e.target.value)} placeholder="chapter ID" />
          </label>
          <label className={styles.field}>
            <span><MapPin size={12} aria-hidden="true" /> World Time</span>
            <input className={styles.input} value={worldTime} onChange={(e) => setWorldTime(e.target.value)} placeholder={state.worldTime ?? 'Day 1 Dawn'} />
          </label>
        </div>

        <label className={styles.field}>
          <span><Users size={12} aria-hidden="true" /> Participants (comma-separated agent IDs)</span>
          <input className={styles.input} value={participantIds} onChange={(e) => setParticipantIds(e.target.value)} placeholder="agent IDs..." />
        </label>

        <div className={styles.row}>
          <label className={styles.field}>
            <span>Section ID</span>
            <input className={styles.input} value={sectionId} onChange={(e) => setSectionId(e.target.value)} placeholder="optional" />
          </label>
          <label className={styles.field}>
            <span>Location ID</span>
            <input className={styles.input} value={locationId} onChange={(e) => setLocationId(e.target.value)} placeholder="optional" />
          </label>
        </div>

        <label className={styles.field}>
          <span>Opening Narrative</span>
          <textarea className={styles.textarea} value={narrative} onChange={(e) => setNarrative(e.target.value)} placeholder="Optional scene opening..." rows={3} />
        </label>

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.secondary} onClick={onClose} disabled={submitting}>Cancel</button>
          <button className={styles.primary} onClick={handleSubmit} disabled={submitting || !title.trim() || !chapterId.trim()}>
            {submitting ? <><Loader2 size={15} aria-hidden="true" className={styles.spin} /> Creating...</> : 'Create Scene'}
          </button>
        </div>
      </section>
    </div>
  );
}
