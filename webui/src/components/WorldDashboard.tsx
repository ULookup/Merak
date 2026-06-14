import { CircleHelp } from 'lucide-react';
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

interface WorldDashboardProps {
  onOpenGuide?: () => void;
}

export default function WorldDashboard({ onOpenGuide }: WorldDashboardProps) {
  const { state, dispatch } = useAppState();

  const currentWorld = state.worlds.find((w) => w.id === state.worldId);
  const storyOverview = state.storyOverview;

  const chapterCount = storyOverview?.current_chapter ? storyOverview.current_chapter.number : 0;
  const sceneCount = storyOverview?.current_scene ? 1 : 0;

  function handleBack() {
    dispatch({ type: 'SET_APP_PHASE', phase: 'no_world' });
    dispatch({ type: 'SET_WORLD', worldId: null });
  }

  const isLoading = state.worldbuildingStatus === 'loading';
  const godAgent = state.agents.filter((a) => a.kind === 'god');
  const characterAgents = state.agents.filter((a) => a.kind === 'individual' || a.kind === 'group');
  const managerAgents = state.agents.filter((a) => a.kind.includes('manager'));
  const activePhaseIndex = state.pipelinePhase ? PIPELINE_PHASES.indexOf(state.pipelinePhase) : -1;

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
            Back to Worlds
          </button>
          <div className={styles.headerText}>
            <h1 className={styles.worldTitle}>{currentWorld?.name ?? 'World Dashboard'}</h1>
            {currentWorld?.description && (
              <p className={styles.worldDesc}>{currentWorld.description}</p>
            )}
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

        {state.worldbuildingError && <div className={styles.error}>{state.worldbuildingError}</div>}
      </div>
    </div>
  );
}
