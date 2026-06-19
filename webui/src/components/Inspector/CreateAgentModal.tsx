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
    function onKey(event: KeyboardEvent) {
      if (event.key === 'Escape' && !submitting) onClose();
    }

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
        core_traits: traits
          ? traits
              .split(',')
              .map((trait) => trait.trim())
              .filter(Boolean)
          : undefined,
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
        <div className={styles.kicker}>Character voice</div>
        <h2>Create Character</h2>
        <p>
          Save the character&apos;s identity, motive, and speaking style so later scenes can keep
          the voice consistent.
        </p>

        <div className={styles.row}>
          <label className={styles.field}>
            <span>Character name</span>
            <input
              className={styles.input}
              value={name}
              onChange={(event) => setName(event.target.value)}
              placeholder="e.g., Lian"
            />
          </label>
          <label className={styles.field}>
            <span>Identity</span>
            <input
              className={styles.input}
              value={identity}
              onChange={(event) => setIdentity(event.target.value)}
              placeholder="e.g., Border medic, exiled heir"
            />
          </label>
        </div>

        <div className={styles.row}>
          <label className={styles.field}>
            <span>Gender</span>
            <input
              className={styles.input}
              value={gender}
              onChange={(event) => setGender(event.target.value)}
              placeholder="Optional"
            />
          </label>
          <label className={styles.field}>
            <span>Age</span>
            <input
              className={styles.input}
              type="number"
              value={age}
              onChange={(event) => setAge(event.target.value)}
              placeholder="28"
            />
          </label>
        </div>

        <label className={styles.field}>
          <span>Race / group</span>
          <input
            className={styles.input}
            value={race}
            onChange={(event) => setRace(event.target.value)}
            placeholder="e.g., Human, north-clan, changeling"
          />
        </label>

        <label className={styles.field}>
          <span>Core desire</span>
          <input
            className={styles.input}
            value={coreDesire}
            onChange={(event) => setCoreDesire(event.target.value)}
            placeholder="What does this character want most?"
          />
        </label>

        <label className={styles.field}>
          <span>Deep fear</span>
          <input
            className={styles.input}
            value={deepFear}
            onChange={(event) => setDeepFear(event.target.value)}
            placeholder="What are they afraid to lose or face?"
          />
        </label>

        <label className={styles.field}>
          <span>Speaking style</span>
          <input
            className={styles.input}
            value={speakingStyle}
            onChange={(event) => setSpeakingStyle(event.target.value)}
            placeholder="e.g., restrained, precise, short sentences"
          />
        </label>

        <label className={styles.field}>
          <span>Background</span>
          <textarea
            className={styles.textarea}
            value={background}
            onChange={(event) => setBackground(event.target.value)}
            placeholder="Briefly describe history, relationships, or wounds."
            rows={3}
          />
        </label>

        <label className={styles.field}>
          <span>Core traits, comma-separated</span>
          <input
            className={styles.input}
            value={traits}
            onChange={(event) => setTraits(event.target.value)}
            placeholder="brave, observant, impulsive"
          />
        </label>

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.secondary} onClick={onClose} disabled={submitting}>
            Cancel
          </button>
          <button className={styles.primary} onClick={handleSubmit} disabled={submitting || !name.trim()}>
            {submitting ? (
              <>
                <Loader2 size={15} aria-hidden="true" className={styles.spin} /> Creating...
              </>
            ) : (
              <>
                <Sparkles size={14} aria-hidden="true" /> Create character
              </>
            )}
          </button>
        </div>
      </section>
    </div>
  );
}
