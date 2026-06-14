import { AlertTriangle, BookOpen, Clock3, Flag, GitBranch, KeyRound, Plus, Users } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import type { DiaryEntry, StoryScene, WorldDetail } from '../../api/types';
import { useAppState } from '../../AppState';
import styles from '../InspectorPanel.module.css';
import CreateForeshadowingModal from './CreateForeshadowingModal';
import CreateSceneModal from './CreateSceneModal';
import CreateSecretModal from './CreateSecretModal';
import EndSceneModal from './EndSceneModal';

const MOOD_COLORS: Record<string, { bg: string; fg: string }> = {
  '喜悦': { bg: '#dcfce7', fg: '#166534' },
  '悲伤': { bg: '#dbeafe', fg: '#1e40af' },
  '愤怒': { bg: '#fee2e2', fg: '#991b1b' },
  '恐惧': { bg: '#fff7ed', fg: '#9a3412' },
  '期待': { bg: '#f3e8ff', fg: '#6b21a8' },
  '困惑': { bg: '#fef9c3', fg: '#854d0e' },
  '决心': { bg: '#ccfbf1', fg: '#115e59' },
  '平静': { bg: '#f3f4f6', fg: '#374151' },
};

type SceneWithNarrative = StoryScene & {
  narrative?: string;
  text?: string;
  content?: string;
};

function moodStyle(mood: string): React.CSSProperties {
  const c = MOOD_COLORS[mood];
  if (c) return { background: c.bg, color: c.fg };
  return { background: '#f3f4f6', color: '#6b7280' };
}

function leakBadge(level: number) {
  if (level === 0) return null;
  const color = level >= 2 ? '#dc2626' : '#d97706';
  const label = level >= 2 ? `泄密高风险` : `泄密低风险`;
  return (
    <span
      style={{
        display: 'inline-flex',
        alignItems: 'center',
        gap: 4,
        fontSize: 11,
        fontWeight: 700,
        color,
        marginLeft: 8,
      }}
    >
      <AlertTriangle size={12} aria-hidden="true" />
      {label}
    </span>
  );
}

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

  // Find the first writing/draft scene
  const activeScene = (() => {
    if (overview?.current_scene?.status === 'writing' || overview?.current_scene?.status === 'draft') {
      return overview.current_scene as SceneWithNarrative;
    }
    return null;
  })();

  const participantAgents = state.agents.filter((a) =>
    activeScene?.participant_ids?.includes(a.id)
  );

  const [participantDiaries, setParticipantDiaries] = useState<DiaryEntry[]>([]);
  const [diariesLoading, setDiariesLoading] = useState(false);

  useEffect(() => {
    if (!state.worldId || participantAgents.length === 0) {
      setParticipantDiaries([]);
      return;
    }
    let cancelled = false;
    setDiariesLoading(true);
    Promise.all(
      participantAgents.map((agent) =>
        api.fetchDiaries(state.worldId!, agent.id).catch(() => ({ ok: true, diaries: [] as DiaryEntry[] }))
      )
    )
      .then((results) => {
        if (cancelled) return;
        const all: DiaryEntry[] = [];
        for (const r of results) {
          all.push(...(r.diaries ?? []));
        }
        all.sort((a, b) => new Date(b.created_at).getTime() - new Date(a.created_at).getTime());
        setParticipantDiaries(all.slice(0, 20));
      })
      .catch(() => {})
      .finally(() => {
        if (!cancelled) setDiariesLoading(false);
      });
    return () => { cancelled = true; };
  }, [state.worldId, activeScene?.id, state.storyVersion]);

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
        <div className={styles.timeUnavailable}>
          <div>
            <strong>世界时间控制</strong>
            <span>{state.worldTime ? `当前：${state.worldTime}` : 'Time not set'}</span>
          </div>
          <button className={styles.ghostButton} disabled title="后端暂未实现 /time/advance">
            <Clock3 size={14} aria-hidden="true" />
            Advance unavailable
          </button>
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

      {activeScene && (
        <section className={styles.section}>
          <div className={styles.sectionTitle}>Scene Narrative</div>
          <div className={styles.narrativeBox}>
            {activeScene.narrative || activeScene.text || activeScene.content || 'No narrative content yet.'}
          </div>
          {participantAgents.length > 0 && (
            <>
              <div className={styles.sectionTitle} style={{ marginTop: '0.75rem' }}>Participants</div>
              <div className={styles.voiceStrip}>
                {participantAgents.map((agent) => (
                  <span key={agent.id}>
                    {agent.display_name || agent.name}
                  </span>
                ))}
              </div>
            </>
          )}
        </section>
      )}

      {activeScene && (
        <section className={styles.section}>
          <div className={styles.sectionTitle}>Character Diaries</div>
          {diariesLoading ? (
            <p className={styles.muted}>Loading diaries...</p>
          ) : participantDiaries.length === 0 ? (
            <p className={styles.muted}>No diary entries for scene participants yet.</p>
          ) : (
            participantDiaries.slice(0, 10).map((diary) => {
              const author = participantAgents.find((a) => a.id === diary.agent_id);
              return (
                <div className={styles.thread} key={diary.id}>
                  <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 4, marginBottom: 4 }}>
                    {author && (
                      <span style={{ display: 'inline-block', color: 'var(--teal)', fontWeight: 800, marginRight: 6, fontSize: 11 }}>
                        {author.display_name || author.name}
                      </span>
                    )}
                    {diary.mood && (
                      <span
                        style={{
                          display: 'inline-block',
                          fontSize: 10,
                          fontWeight: 700,
                          padding: '2px 6px',
                          borderRadius: 999,
                          ...moodStyle(diary.mood),
                        }}
                      >
                        {diary.mood}
                      </span>
                    )}
                    {leakBadge(diary.leak_risk_level)}
                    {diary.world_time && (
                      <small style={{ marginLeft: 'auto', color: 'var(--muted)', fontSize: 10 }}>
                        {diary.world_time}
                      </small>
                    )}
                  </div>
                  <div style={{ fontSize: 12, lineHeight: 1.5, color: 'var(--ink)' }}>
                    {diary.content.length > 200 ? diary.content.slice(0, 200) + '...' : diary.content}
                  </div>
                </div>
              );
            })
          )}
        </section>
      )}

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
