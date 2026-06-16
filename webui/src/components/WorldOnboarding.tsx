import { useState } from 'react';
import { ChevronDown, ChevronRight, CircleHelp, Globe2, Plus, Sparkles } from 'lucide-react';
import { api } from '../api/client';
import { useAppState } from '../AppState';
import { LanguageToggle, useI18n } from '../i18n';
import styles from './WorldOnboarding.module.css';

interface WorldOnboardingProps {
  onOpenGuide?: () => void;
}

function formatWorldTime(value: string | undefined) {
  if (!value) return 'Not opened yet';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleString(undefined, {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  });
}

export default function WorldOnboarding({ onOpenGuide }: WorldOnboardingProps) {
  const { state, dispatch } = useAppState();
  const { t } = useI18n();
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [creating, setCreating] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [showCreate, setShowCreate] = useState(false);
  const [showCharacterForm, setShowCharacterForm] = useState(false);
  const [characterName, setCharacterName] = useState('');
  const [characterIdentity, setCharacterIdentity] = useState('');

  const existingWorlds = state.worlds.filter((world) => world.id);
  const createMode = existingWorlds.length === 0 || showCreate;

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
      if (!worldId) throw new Error('Failed to get world ID from response.');
      const nextWorld = {
        id: worldId,
        name: world.name || name.trim(),
        description: world.description ?? description.trim(),
        created_at: new Date().toISOString(),
      };

      if (characterName.trim()) {
        try {
          await api.createAgent(worldId, {
            name: characterName.trim(),
            identity: characterIdentity.trim() || 'A character in this world.',
          });
        } catch {
          // Keep the first-run path moving; the character can be created later in the workbench.
        }
      }

      dispatch({
        type: 'SET_WORLDS',
        worlds: [nextWorld, ...state.worlds.filter((existing) => existing.id !== worldId)],
      });
      dispatch({ type: 'SET_WORLD', worldId });
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : 'Failed to create world.');
    } finally {
      setCreating(false);
    }
  }

  function selectExistingWorld(worldId: string) {
    dispatch({ type: 'SET_WORLD', worldId });
  }

  return (
    <div className={styles.onboarding}>
      <div className={styles.ambient} aria-hidden="true">
        <span />
        <span />
        <span />
      </div>
      <main className={styles.shell}>
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

        <header className={styles.header}>
          <div className={styles.heroMark} aria-hidden="true">
            <Globe2 size={28} strokeWidth={1.8} />
          </div>
          <div>
            <h1 className={styles.title}>{createMode ? t('onboarding.title') : 'Merak Worlds'}</h1>
            <p className={styles.subtitle}>
              {createMode
                ? t('onboarding.subtitle')
                : 'Choose a world to continue writing, or create a new one when the shelf is empty.'}
            </p>
          </div>
        </header>

        {!createMode && (
          <section className={styles.entryPanel} aria-label="World entry">
            <div className={styles.panelHeader}>
              <div>
                <div className={styles.sectionKicker}>Continue a World</div>
                <h2>Pick up where the story left off.</h2>
              </div>
              <button
                className={styles.secondaryBtn}
                type="button"
                onClick={() => setShowCreate(true)}
                aria-label="Create new world"
              >
                <Plus size={15} aria-hidden="true" strokeWidth={2.3} />
                New World
              </button>
            </div>

            <div className={styles.worldGrid}>
              {existingWorlds.map((world) => (
                <button
                  key={world.id}
                  className={styles.worldCard}
                  type="button"
                  onClick={() => selectExistingWorld(world.id)}
                  aria-label={`Enter ${world.name}`}
                >
                  <span className={styles.worldCover} aria-hidden="true">
                    <Globe2 size={22} strokeWidth={1.8} />
                  </span>
                  <span className={styles.worldBody}>
                    <span className={styles.worldMeta}>
                      <span>Ready to enter</span>
                      <span>Last opened {formatWorldTime(world.updated_at || world.created_at)}</span>
                    </span>
                    <strong>{world.name}</strong>
                    {world.description && <small>{world.description}</small>}
                  </span>
                  <span className={styles.enterCue} aria-hidden="true">
                    Enter
                    <ChevronRight size={17} strokeWidth={2.3} />
                  </span>
                </button>
              ))}
            </div>
          </section>
        )}

        {createMode && (
          <section className={styles.createPanel} aria-label="Create world">
            <div className={styles.panelHeader}>
              <div>
                <div className={styles.sectionKicker}>
                  {existingWorlds.length > 0 ? 'New World' : 'Create your first World'}
                </div>
                <h2>
                  {existingWorlds.length > 0 ? 'Start a separate story space.' : t('onboarding.subtitle')}
                </h2>
              </div>
              {existingWorlds.length > 0 && (
                <button
                  className={styles.secondaryBtn}
                  type="button"
                  onClick={() => setShowCreate(false)}
                >
                  Back to Worlds
                </button>
              )}
            </div>

            <div className={styles.field}>
              <label className={styles.label} htmlFor="world-name">
                {t('onboarding.worldName')}
                <span aria-hidden="true"> *</span>
              </label>
              <input
                id="world-name"
                className={styles.input}
                type="text"
                aria-label={t('onboarding.worldName')}
                value={name}
                onChange={(event) => setName(event.target.value)}
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
                onChange={(event) => setDescription(event.target.value)}
                placeholder={t('onboarding.descriptionPlaceholder')}
                disabled={creating}
              />
            </div>

            <div className={styles.toggleSection}>
              <button
                className={styles.toggleBtn}
                onClick={() => setShowCharacterForm((prev) => !prev)}
                type="button"
                aria-expanded={showCharacterForm}
                aria-controls="first-character-fields"
              >
                {showCharacterForm ? (
                  <ChevronDown size={16} aria-hidden="true" />
                ) : (
                  <ChevronRight size={16} aria-hidden="true" />
                )}
                {t('onboarding.firstCharacter')}
              </button>

              {showCharacterForm && (
                <div className={styles.characterForm} id="first-character-fields">
                  <div className={styles.field}>
                    <label className={styles.label} htmlFor="character-name">
                      {t('onboarding.characterName')}
                    </label>
                    <input
                      id="character-name"
                      className={styles.input}
                      type="text"
                      value={characterName}
                      onChange={(event) => setCharacterName(event.target.value)}
                      placeholder={t('onboarding.characterNamePlaceholder')}
                      disabled={creating}
                    />
                  </div>
                  <div className={styles.field}>
                    <label className={styles.label} htmlFor="character-identity">
                      {t('onboarding.identity')}
                    </label>
                    <input
                      id="character-identity"
                      className={styles.input}
                      type="text"
                      value={characterIdentity}
                      onChange={(event) => setCharacterIdentity(event.target.value)}
                      placeholder={t('onboarding.identityPlaceholder')}
                      disabled={creating}
                    />
                  </div>
                </div>
              )}
            </div>

            <button
              className={styles.submitBtn}
              type="button"
              onClick={handleCreate}
              disabled={creating || !name.trim()}
            >
              <Sparkles size={16} aria-hidden="true" />
              {creating ? t('onboarding.creating') : t('onboarding.createWorld')}
            </button>

            {error && <div className={styles.error}>{error}</div>}
          </section>
        )}
      </main>
    </div>
  );
}
