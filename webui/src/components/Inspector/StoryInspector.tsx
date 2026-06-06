import { BookOpen, Clock3, GitBranch, KeyRound, Users } from 'lucide-react';
import { useAppState } from '../../AppState';
import styles from '../InspectorPanel.module.css';

function statusLabel(value: string | undefined) {
  return value ? value.replace(/_/g, ' ') : 'open';
}

export default function StoryInspector() {
  const { state } = useAppState();
  const selectedWorld = state.worlds.find((world) => world.id === state.worldId);
  const overview = state.storyOverview;
  const chapter = overview?.current_chapter;
  const scene = overview?.current_scene;

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
            {state.agents.length} voices
          </span>
          <span>
            <GitBranch size={14} aria-hidden="true" />
            {state.foreshadowing.length} threads
          </span>
        </div>
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
      </section>

      <section className={styles.section}>
        <div className={styles.sectionTitle}>Active Voices</div>
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
        <div className={styles.sectionTitle}>
          <BookOpen size={14} aria-hidden="true" />
          Open Foreshadowing
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
        <div className={styles.sectionTitle}>
          <KeyRound size={14} aria-hidden="true" />
          Knowledge Boundaries
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
    </>
  );
}
