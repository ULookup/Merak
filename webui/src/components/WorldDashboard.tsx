import { useEffect, useState } from 'react';
import { api } from '../api/client';
import { useAppState } from '../AppState';
import AgentCard from './AgentCard';
import styles from './WorldDashboard.module.css';

const PIPELINE_PHASES = [
  'worldbuilding',
  'character_creation',
  'plot_architecture',
  'scene_writing',
  'reflection',
];

export default function WorldDashboard() {
  const { state, dispatch } = useAppState();

  const currentWorld = state.worlds.find((w) => w.id === state.worldId);
  const storyOverview = state.storyOverview;

  // Chapter and scene counts loaded via listChapters API
  const [chapterCount, setChapterCount] = useState(0);
  const [sceneCount, setSceneCount] = useState(0);

  // Load dashboard data when worldId is set
  useEffect(() => {
    if (!state.worldId) return;

    dispatch({ type: 'SET_WORLDBUILDING_STATUS', status: 'loading' });

    Promise.allSettled([
      api.getStoryOverview(state.worldId, state.sessionId),
      api.listAgents(state.worldId),
      api.listWorlds(),
      api.listChapters(state.worldId),
    ]).then(([overviewRes, agentsRes, worldsRes, chaptersRes]) => {
      const overview = overviewRes.status === 'fulfilled' ? overviewRes.value.overview : null;
      const agents = agentsRes.status === 'fulfilled' ? (agentsRes.value.agents ?? []) : [];
      const worlds =
        worldsRes.status === 'fulfilled' ? (worldsRes.value.worlds ?? state.worlds) : state.worlds;

      dispatch({
        type: 'SET_WORLDBUILDING_DATA',
        worlds,
        agents,
        foreshadowing: overview?.foreshadowing ?? [],
        secrets: overview?.secrets ?? [],
        worldTime: overview?.world_time ?? null,
        storyOverview: overview,
      });

      // Compute chapter and scene counts from chapters list
      if (chaptersRes.status === 'fulfilled') {
        const chapters = chaptersRes.value.chapters ?? [];
        setChapterCount(chapters.length);
        setSceneCount(chapters.reduce((sum, ch) => sum + (ch.scene_count ?? 0), 0));
      }
    });
  }, [state.worldId]);

  function handleBack() {
    dispatch({ type: 'SET_APP_PHASE', phase: 'no_world' });
    dispatch({ type: 'SET_WORLD', worldId: null });
  }

  const isLoading = state.worldbuildingStatus === 'loading';

  // Separate agents by kind (string-based; actual API uses string kinds)
  const godAgent = state.agents.filter((a) => a.kind === 'god');
  const characterAgents = state.agents.filter(
    (a) => a.kind === 'individual' || a.kind === 'group'
  );
  const managerAgents = state.agents.filter((a) => a.kind.includes('manager'));

  const activePhaseIndex = state.pipelinePhase
    ? PIPELINE_PHASES.indexOf(state.pipelinePhase)
    : -1;

  if (isLoading) {
    return (
      <div className={styles.dashboard}>
        <div className={styles.container}>
          <div className={styles.loading}>Loading world data...</div>
        </div>
      </div>
    );
  }

  return (
    <div className={styles.dashboard}>
      <div className={styles.container}>
        <div className={styles.header}>
          <button className={styles.backBtn} onClick={handleBack}>
            ← Back to Worlds
          </button>
          <div>
            <h1 className={styles.worldTitle}>
              {currentWorld?.name ?? 'World Dashboard'}
            </h1>
            {currentWorld?.description && (
              <p className={styles.worldDesc}>{currentWorld.description}</p>
            )}
          </div>
        </div>

        <div className={styles.stats}>
          <div className={styles.statCard}>
            <div className={styles.statValue}>{chapterCount}</div>
            <div className={styles.statLabel}>Chapters</div>
          </div>
          <div className={styles.statCard}>
            <div className={styles.statValue}>{sceneCount}</div>
            <div className={styles.statLabel}>Scenes</div>
          </div>
          <div className={styles.statCard}>
            <div className={styles.statValue}>{characterAgents.length + managerAgents.length}</div>
            <div className={styles.statLabel}>Characters</div>
          </div>
          <div className={styles.statCard}>
            <div className={styles.statValue}>{storyOverview?.foreshadowing?.length ?? 0}</div>
            <div className={styles.statLabel}>Foreshadowing</div>
          </div>
        </div>

        <div className={styles.pipeline}>
          <p className={styles.pipelineLabel}>Story Pipeline</p>
          <div className={styles.pipelineBar}>
            {PIPELINE_PHASES.map((phase, i) => (
              <div
                key={phase}
                className={`${styles.pipelinePhase} ${
                  i <= activePhaseIndex ? styles.pipelinePhaseActive : styles.pipelinePhaseInactive
                }`}
              />
            ))}
          </div>
        </div>

        {godAgent.length > 0 && (
          <div className={styles.section}>
            <h2 className={styles.sectionTitle}>Creator</h2>
            <div className={styles.agentGrid}>
              {godAgent.map((agent) => (
                <AgentCard key={agent.id} agent={agent} worldId={state.worldId!} />
              ))}
            </div>
          </div>
        )}

        {characterAgents.length > 0 && (
          <div className={styles.section}>
            <h2 className={styles.sectionTitle}>Characters</h2>
            <div className={styles.agentGrid}>
              {characterAgents.map((agent) => (
                <AgentCard key={agent.id} agent={agent} worldId={state.worldId!} />
              ))}
            </div>
          </div>
        )}

        {managerAgents.length > 0 && (
          <div className={styles.section}>
            <h2 className={styles.sectionTitle}>Domain Managers</h2>
            <div className={styles.agentGrid}>
              {managerAgents.map((agent) => (
                <AgentCard key={agent.id} agent={agent} worldId={state.worldId!} />
              ))}
            </div>
          </div>
        )}

        {state.worldbuildingError && (
          <div className={styles.error}>{state.worldbuildingError}</div>
        )}
      </div>
    </div>
  );
}
