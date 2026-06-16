import { useState } from 'react';
import { CircleHelp } from 'lucide-react';
import { api } from '../api/client';
import { useAppState } from '../AppState';
import { LanguageToggle, useI18n } from '../i18n';
import styles from './WorldOnboarding.module.css';

interface WorldOnboardingProps {
  onOpenGuide?: () => void;
}

export default function WorldOnboarding({ onOpenGuide }: WorldOnboardingProps) {
  const { state, dispatch } = useAppState();
  const { t } = useI18n();
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [creating, setCreating] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [showCharacterForm, setShowCharacterForm] = useState(false);
  const [characterName, setCharacterName] = useState('');
  const [characterIdentity, setCharacterIdentity] = useState('');

  async function handleCreate() {
    if (!name.trim()) {
      setError(t('onboarding.nameRequired'));
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
          // Character creation failed but world exists; continue.
        }
      }

      dispatch({ type: 'SET_WORLD', worldId });
    } catch (error: unknown) {
      setError(error instanceof Error ? error.message : 'Failed to create world.');
    } finally {
      setCreating(false);
    }
  }

  function selectExistingWorld(worldId: string) {
    dispatch({ type: 'SET_WORLD', worldId });
  }

  const existingWorlds = state.worlds.filter((w) => w.id);

  return (
    <div className={styles.onboarding}>
      <div className={styles.card}>
        <div className={styles.topbar}>
          <div className={styles.languageToggle}>
            <LanguageToggle />
          </div>
          <button
            type="button"
            className={styles.helpBtn}
            onClick={onOpenGuide}
            aria-label={t('app.openGuide')}
            title={t('app.openGuide')}
          >
            <CircleHelp size={16} aria-hidden="true" strokeWidth={2.2} />
          </button>
        </div>

        <h1 className={styles.title}>{t('onboarding.title')}</h1>
        <p className={styles.subtitle}>{t('onboarding.subtitle')}</p>

        <div className={styles.field}>
          <label className={styles.label} htmlFor="world-name">
            {t('onboarding.worldName')}
          </label>
          <input
            id="world-name"
            className={styles.input}
            type="text"
            value={name}
            onChange={(e) => setName(e.target.value)}
            placeholder={t('onboarding.worldNamePlaceholder')}
            disabled={creating}
            autoFocus
          />
        </div>

        <div className={styles.field}>
          <label className={styles.label} htmlFor="world-description">
            {t('onboarding.description')}
          </label>
          <textarea
            id="world-description"
            className={styles.textarea}
            value={description}
            onChange={(e) => setDescription(e.target.value)}
            placeholder={t('onboarding.descriptionPlaceholder')}
            disabled={creating}
          />
        </div>

        <div className={styles.toggleSection}>
          <button
            className={styles.toggleBtn}
            onClick={() => setShowCharacterForm((prev) => !prev)}
            type="button"
          >
            {showCharacterForm ? '-' : '+'} {t('onboarding.firstCharacter')}
          </button>

          {showCharacterForm && (
            <div className={styles.characterForm}>
              <div className={styles.field}>
                <label className={styles.label}>{t('onboarding.characterName')}</label>
                <input
                  className={styles.input}
                  type="text"
                  value={characterName}
                  onChange={(e) => setCharacterName(e.target.value)}
                  placeholder={t('onboarding.characterNamePlaceholder')}
                  disabled={creating}
                />
              </div>
              <div className={styles.field}>
                <label className={styles.label}>{t('onboarding.identity')}</label>
                <input
                  className={styles.input}
                  type="text"
                  value={characterIdentity}
                  onChange={(e) => setCharacterIdentity(e.target.value)}
                  placeholder={t('onboarding.identityPlaceholder')}
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
          {creating ? t('onboarding.creating') : t('onboarding.createWorld')}
        </button>

        {error && <div className={styles.error}>{error}</div>}

        {existingWorlds.length > 0 && (
          <div className={styles.existingWorlds}>
            <p className={styles.existingTitle}>{t('onboarding.existingWorlds')}</p>
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
