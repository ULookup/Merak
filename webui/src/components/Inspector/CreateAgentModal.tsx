import { Loader2, Sparkles, UserPlus, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from './CreateModal.module.css';

interface Props {
  worldId: string;
  onClose: () => void;
  onCreated?: () => void;
}

export default function CreateAgentModal({ worldId, onClose, onCreated }: Props) {
  const { state } = useAppState();
  const [name, setName] = useState('');
  const [identity, setIdentity] = useState('');
  const [gender, setGender] = useState('');
  const [age, setAge] = useState('');
  const [race, setRace] = useState('');
  const [coreDesire, setCoreDesire] = useState('');
  const [deepFear, setDeepFear] = useState('');
  const [speakingStyle, setSpeakingStyle] = useState('');
  const [background, setBackground] = useState('');
  const [traits, setTraits] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    function onKey(e: KeyboardEvent) { if (e.key === 'Escape' && !submitting) onClose(); }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose, submitting]);

  async function handleSubmit() {
    if (!name.trim()) return;
    setSubmitting(true);
    setError(null);
    try {
      await api.createAgent(worldId, {
        name: name.trim(),
        identity: identity.trim() || undefined,
        gender: gender.trim() || undefined,
        age: age ? parseInt(age, 10) : undefined,
        race: race.trim() || undefined,
        core_desire: coreDesire.trim() || undefined,
        deep_fear: deepFear.trim() || undefined,
        speaking_style: speakingStyle.trim() || undefined,
        background: background.trim() || undefined,
        core_traits: traits ? traits.split(',').map(t => t.trim()).filter(Boolean) : undefined,
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
      <section className={styles.modal} role="dialog" aria-modal="true" aria-label="Create character">
        <button className={styles.closeBtn} onClick={onClose} aria-label="Cancel" disabled={submitting}>
          <X size={17} aria-hidden="true" strokeWidth={2.4} />
        </button>
        <div className={styles.iconWrap}>
          <UserPlus size={28} aria-hidden="true" strokeWidth={2.1} />
        </div>
        <div className={styles.kicker}>New Voice</div>
        <h2>Create Character</h2>
        <p>Add a new character to this world. The more detail you provide, the more consistent their voice will be in scenes.</p>

        <div className={styles.row}>
          <label className={styles.field}>
            <span>Name (ID)</span>
            <input className={styles.input} value={name} onChange={(e) => setName(e.target.value)} placeholder="e.g. elara_moonshadow" />
          </label>
          <label className={styles.field}>
            <span>Identity</span>
            <input className={styles.input} value={identity} onChange={(e) => setIdentity(e.target.value)} placeholder="Thief, Scholar..." />
          </label>
        </div>

        <div className={styles.row}>
          <label className={styles.field}>
            <span>Gender</span>
            <input className={styles.input} value={gender} onChange={(e) => setGender(e.target.value)} placeholder="Female / Male / ..." />
          </label>
          <label className={styles.field}>
            <span>Age</span>
            <input className={styles.input} type="number" value={age} onChange={(e) => setAge(e.target.value)} placeholder="28" />
          </label>
        </div>

        <label className={styles.field}>
          <span>Race</span>
          <input className={styles.input} value={race} onChange={(e) => setRace(e.target.value)} placeholder="Human, Elf, ..." />
        </label>

        <label className={styles.field}>
          <span>Core Desire</span>
          <input className={styles.input} value={coreDesire} onChange={(e) => setCoreDesire(e.target.value)} placeholder="What drives them..." />
        </label>

        <label className={styles.field}>
          <span>Deep Fear</span>
          <input className={styles.input} value={deepFear} onChange={(e) => setDeepFear(e.target.value)} placeholder="What they're most afraid of..." />
        </label>

        <label className={styles.field}>
          <span>Speaking Style</span>
          <input className={styles.input} value={speakingStyle} onChange={(e) => setSpeakingStyle(e.target.value)} placeholder="Formal, casual, terse..." />
        </label>

        <label className={styles.field}>
          <span>Background</span>
          <textarea className={styles.textarea} value={background} onChange={(e) => setBackground(e.target.value)} placeholder="A brief history..." rows={3} />
        </label>

        <label className={styles.field}>
          <span>Core Traits (comma-separated)</span>
          <input className={styles.input} value={traits} onChange={(e) => setTraits(e.target.value)} placeholder="brave, loyal, reckless" />
        </label>

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.secondary} onClick={onClose} disabled={submitting}>Cancel</button>
          <button className={styles.primary} onClick={handleSubmit} disabled={submitting || !name.trim()}>
            {submitting ? <><Loader2 size={15} aria-hidden="true" className={styles.spin} /> Creating...</> : <><Sparkles size={14} aria-hidden="true" /> Create Character</>}
          </button>
        </div>
      </section>
    </div>
  );
}
