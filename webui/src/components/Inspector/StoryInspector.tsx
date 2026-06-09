import { BookOpen, Clock3, Flag, GitBranch, KeyRound, Plus, Users } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import type { WorldDetail } from '../../api/types';
import { useAppState } from '../../AppState';
import styles from '../InspectorPanel.module.css';
import CreateForeshadowingModal from './CreateForeshadowingModal';
import CreateSceneModal from './CreateSceneModal';
import CreateSecretModal from './CreateSecretModal';
import EndSceneModal from './EndSceneModal';

function statusLabel(value: string | undefined) {
  return value ? value.replace(/_/g, ' ') : 'open';
}

export default function StoryInspector() {
  const { state, dispatch } = useAppState();
  const selectedWorld = state.worlds.find((world) => world.id === state.worldId);
  const overview = state.storyOverview;
  const chapter = overview?.current_chapter;
  const scene = overview?.current_scene;

  const [showEndScene, setShowEndScene] = useState(false);
  const [showCreateForeshadowing, setShowCreateForeshadowing] = useState(false);
  const [showCreateSecret, setShowCreateSecret] = useState(false);
  const [showCreateScene, setShowCreateScene] = useState(false);
  const [worldDetail, setWorldDetail] = useState<WorldDetail | null>(null);

  useEffect(() => {
    if (!state.worldId) { setWorldDetail(null); return; }
    api.getWorldDetail(state.worldId)
      .then(res => setWorldDetail(res.world))
      .catch(() => setWorldDetail(null));
  }, [state.worldId, state.storyVersion]);

  function handleCreated() {
    dispatch({ type: 'SET_STORY_VERSION' });
  }

  return (
    <>
      {state.fallback.storyOverview && (
        <div className={styles.notice}>Story overview is using WebUI preview data.</div>
      )}

      <section className={styles.heroBlock}>
        <div className={styles.worldName}>{selectedWorld?.name ?? state.worldId}</div>
        <p>{selectedWorld?.description || 'No world description yet.'}</p>
        <div className={styles.storyStats}>
          <span>
            <Clock3 size={14} aria-hidden="true" />
            {state.worldTime ?? 'Time not set'}
          </span>
          <span>
            <Users size={14} aria-hidden="true" />
            {worldDetail ? worldDetail.stats.agents : state.agents.length} voices
          </span>
          <span>
            <GitBranch size={14} aria-hidden="true" />
            {worldDetail ? worldDetail.stats.open_foreshadowing : state.foreshadowing.length} threads
          </span>
        </div>
        {worldDetail && (
          <div className={styles.storyStats} style={{ marginTop: 8 }}>
            <span>
              <BookOpen size={14} aria-hidden="true" />
              {worldDetail.stats.chapters} chapters
            </span>
            <span>
              <GitBranch size={14} aria-hidden="true" />
              {worldDetail.stats.scenes} scenes
            </span>
            <span>
              <KeyRound size={14} aria-hidden="true" />
              {worldDetail.stats.active_secrets} secrets
            </span>
          </div>
        )}
      </section>

      <section className={styles.section}>
        <div className={styles.sectionTitle}>Narrative Position</div>
        <div className={styles.contextGrid}>
          <div>
            <span>Arc</span>
            <strong>{overview?.current_arc?.title ?? 'Unassigned arc'}</strong>
            <small>{overview?.current_arc?.status ?? 'freeform'}</small>
          </div>
          <div>
            <span>Chapter</span>
            <strong>{chapter ? `${chapter.number}. ${chapter.title}` : 'No chapter selected'}</strong>
            <small>{chapter ? `${chapter.scene_count} scenes` : 'waiting for backend'}</small>
          </div>
          <div>
            <span>Scene</span>
            <strong>{scene?.title ?? 'No active scene'}</strong>
            <small>{scene?.status ?? 'draft'}</small>
          </div>
        </div>
        {scene && state.worldId && chapter && (
          <div className={styles.sceneActions}>
            <button
              className={styles.entryButton}
              onClick={() => setShowEndScene(true)}
            >
              <Flag size={14} aria-hidden="true" />
              End Scene
            </button>
            <button
              className={styles.ghostButton}
              onClick={() => setShowCreateScene(true)}
            >
              <Plus size={14} aria-hidden="true" />
              New Scene
            </button>
          </div>
        )}
      </section>

      <section className={styles.section}>
        <div className={styles.sectionHeader}>
          <div className={styles.sectionTitle}>Active Voices</div>
        </div>
        {state.agents.length === 0 ? (
          <p className={styles.muted}>No character voices loaded.</p>
        ) : (
          <div className={styles.voiceStrip}>
            {state.agents.slice(0, 6).map((agent) => (
              <span key={agent.id}>{agent.display_name || agent.name}</span>
            ))}
          </div>
        )}
      </section>

      <section className={styles.section}>
        <div className={styles.sectionHeader}>
          <div className={styles.sectionTitle}>
            <BookOpen size={14} aria-hidden="true" />
            Open Foreshadowing
          </div>
          <button
            className={styles.addBtn}
            onClick={() => setShowCreateForeshadowing(true)}
            aria-label="Plant foreshadowing"
            title="Plant new thread"
          >
            <Plus size={14} aria-hidden="true" />
          </button>
        </div>
        {state.foreshadowing.length === 0 ? (
          <p className={styles.muted}>No open threads loaded.</p>
        ) : (
          state.foreshadowing.slice(0, 6).map((item) => (
            <div className={styles.thread} key={item.id}>
              <span>{statusLabel(item.status)}</span>
              {item.content}
              {item.pay_off_idea && <small>{item.pay_off_idea}</small>}
            </div>
          ))
        )}
      </section>

      <section className={styles.section}>
        <div className={styles.sectionHeader}>
          <div className={styles.sectionTitle}>
            <KeyRound size={14} aria-hidden="true" />
            Knowledge Boundaries
          </div>
          <button
            className={styles.addBtn}
            onClick={() => setShowCreateSecret(true)}
            aria-label="Create secret"
            title="New secret"
          >
            <Plus size={14} aria-hidden="true" />
          </button>
        </div>
        {state.secrets.length === 0 ? (
          <p className={styles.muted}>No secret boundaries loaded.</p>
        ) : (
          state.secrets.slice(0, 5).map((item) => (
            <div className={styles.secret} key={item.id}>
              <strong>{item.title ?? item.content ?? item.id}</strong>
              <small>{item.public_version ?? item.stakes ?? item.truth ?? statusLabel(item.status)}</small>
            </div>
          ))
        )}
      </section>

      {showEndScene && scene && state.worldId && chapter && (
        <EndSceneModal
          worldId={state.worldId}
          sceneId={scene.id}
          sceneTitle={scene.title}
          chapterId={chapter.id}
          onClose={() => setShowEndScene(false)}
        />
      )}
      {showCreateForeshadowing && state.worldId && (
        <CreateForeshadowingModal worldId={state.worldId} onClose={() => setShowCreateForeshadowing(false)} onCreated={handleCreated} />
      )}
      {showCreateSecret && state.worldId && (
        <CreateSecretModal worldId={state.worldId} onClose={() => setShowCreateSecret(false)} onCreated={handleCreated} />
      )}
      {showCreateScene && state.worldId && (
        <CreateSceneModal worldId={state.worldId} onClose={() => setShowCreateScene(false)} onCreated={handleCreated} />
      )}
    </>
  );
}
