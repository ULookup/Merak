import { useState } from 'react';
import { CircleHelp, Minus, Plus } from 'lucide-react';
import { api } from '../api/client';
import { useAppState } from '../AppState';
import styles from './WorldOnboarding.module.css';

interface WorldOnboardingProps {
  onOpenGuide?: () => void;
}

export default function WorldOnboarding({ onOpenGuide }: WorldOnboardingProps) {
  const { state, dispatch } = useAppState();
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [creating, setCreating] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [showCharacterForm, setShowCharacterForm] = useState(false);
  const [characterName, setCharacterName] = useState('');
  const [characterIdentity, setCharacterIdentity] = useState('');

  async function handleCreate() {
    if (!name.trim()) {
      setError('World name is required.');
      return;
    }
    setCreating(true);
    setError(null);

    try {
      const world = await api.createWorld(name.trim(), description.trim() || undefined);
      const worldId = world.world_id;
      if (!worldId) throw new Error('Failed to get world ID from response');

      if (characterName.trim()) {
        try {
          await api.createAgent(worldId, {
            name: characterName.trim(),
            identity: characterIdentity.trim() || 'A character in this world.',
          });
        } catch {
          // Character creation failed but the world can still be opened.
        }
      }

      dispatch({ type: 'SET_WORLD', worldId });
    } catch (e: any) {
      setError(e?.message ?? 'Failed to create world.');
    } finally {
      setCreating(false);
    }
  }

  function selectExistingWorld(worldId: string) {
    dispatch({ type: 'SET_WORLD', worldId });
  }

  const existingWorlds = state.worlds.filter((w) => w.id);
  const ToggleIcon = showCharacterForm ? Minus : Plus;

  return (
    <div className={styles.onboarding}>
      <div className={styles.card}>
        <div className={styles.header}>
          <div>
            <h1 className={styles.title}>Merak Creation Workshop</h1>
            <p className={styles.subtitle}>
              Create a new world to begin your storytelling journey.
            </p>
          </div>
          <button
            className={styles.helpBtn}
            type="button"
            onClick={onOpenGuide}
            aria-label="Open workbench guide"
            title="Open workbench guide"
          >
            <CircleHelp size={18} aria-hidden="true" strokeWidth={2.2} />
          </button>
        </div>

        <div className={styles.field}>
          <label className={styles.label}>World Name *</label>
          <input
            className={styles.input}
            type="text"
            value={name}
            onChange={(e) => setName(e.target.value)}
            placeholder="e.g., Cyberpunk 2077"
            disabled={creating}
            autoFocus
          />
        </div>

        <div className={styles.field}>
          <label className={styles.label}>Description (optional)</label>
          <textarea
            className={styles.textarea}
            value={description}
            onChange={(e) => setDescription(e.target.value)}
            placeholder="A dark future where megacorporations rule..."
            disabled={creating}
          />
        </div>

        <div className={styles.toggleSection}>
          <button
            className={styles.toggleBtn}
            onClick={() => setShowCharacterForm((prev) => !prev)}
            type="button"
          >
            <ToggleIcon size={14} aria-hidden="true" strokeWidth={2.4} />
            Create first character
          </button>

          {showCharacterForm && (
            <div className={styles.characterForm}>
              <div className={styles.field}>
                <label className={styles.label}>Character Name</label>
                <input
                  className={styles.input}
                  type="text"
                  value={characterName}
                  onChange={(e) => setCharacterName(e.target.value)}
                  placeholder="e.g., John Smith"
                  disabled={creating}
                />
              </div>
              <div className={styles.field}>
                <label className={styles.label}>Identity</label>
                <input
                  className={styles.input}
                  type="text"
                  value={characterIdentity}
                  onChange={(e) => setCharacterIdentity(e.target.value)}
                  placeholder="e.g., Private detective in Night City"
                  disabled={creating}
                />
              </div>
            </div>
          )}
        </div>

        <button
          className={styles.submitBtn}
          onClick={handleCreate}
          disabled={creating || !name.trim()}
        >
          {creating ? 'Creating...' : 'Create World'}
        </button>

        {error && <div className={styles.error}>{error}</div>}

        {existingWorlds.length > 0 && (
          <div className={styles.existingWorlds}>
            <p className={styles.existingTitle}>Existing Worlds</p>
            {existingWorlds.map((world) => (
              <div
                key={world.id}
                className={styles.worldItem}
                onClick={() => selectExistingWorld(world.id)}
                role="button"
                tabIndex={0}
                onKeyDown={(e) => {
                  if (e.key === 'Enter') selectExistingWorld(world.id);
                }}
              >
                <div>
                  <div className={styles.worldItemName}>{world.name}</div>
                  {world.description && (
                    <div className={styles.worldItemDesc}>{world.description}</div>
                  )}
                </div>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
