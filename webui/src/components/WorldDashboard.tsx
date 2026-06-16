import {
  ArrowLeft,
  BookOpen,
  CircleHelp,
  PenLine,
  Sparkles,
  UserRoundPlus,
} from 'lucide-react';
import { useState } from 'react';
import { useAppState } from '../AppState';
import AgentCard from './AgentCard';
import CreateAgentModal from './Inspector/CreateAgentModal';
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
  const [showCreateAgent, setShowCreateAgent] = useState(false);

  const currentWorld = state.worlds.find((w) => w.id === state.worldId);
  const storyOverview = state.storyOverview;

  const chapterCount = storyOverview?.current_chapter ? storyOverview.current_chapter.number : 0;
  const sceneCount = storyOverview?.current_scene ? 1 : 0;

  function handleBack() {
    dispatch({ type: 'SET_APP_PHASE', phase: 'no_world' });
    dispatch({ type: 'SET_WORLD', worldId: null });
  }

  function handleAgentCreated() {
    dispatch({ type: 'SET_STORY_VERSION' });
  }

  const isLoading = state.worldbuildingStatus === 'loading';
  const godAgents = state.agents.filter((a) => a.kind === 'god');
  const characterAgents = state.agents.filter((a) => a.kind === 'individual' || a.kind === 'group');
  const managerAgents = state.agents.filter((a) => a.kind?.includes('manager'));
  const totalUsableAgents = characterAgents.length + managerAgents.length + godAgents.length;

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
          <button className={styles.backBtn} onClick={handleBack} aria-label="Back to Worlds">
            <ArrowLeft size={16} aria-hidden="true" />
            Back to Worlds
          </button>
          <div>
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
            <div className={styles.statIcon} aria-hidden="true">
              <BookOpen size={16} />
            </div>
            <div className={styles.statValue}>{chapterCount}</div>
            <div className={styles.statLabel}>Chapters</div>
          </div>
          <div className={styles.statCard}>
            <div className={styles.statIcon} aria-hidden="true">
              <PenLine size={16} />
            </div>
            <div className={styles.statValue}>{sceneCount}</div>
            <div className={styles.statLabel}>Scenes</div>
          </div>
          <div className={styles.statCard}>
            <div className={styles.statIcon} aria-hidden="true">
              <UserRoundPlus size={16} />
            </div>
            <div className={styles.statValue}>{characterAgents.length + managerAgents.length}</div>
            <div className={styles.statLabel}>Characters</div>
          </div>
          <div className={styles.statCard}>
            <div className={styles.statIcon} aria-hidden="true">
              <Sparkles size={16} />
            </div>
            <div className={styles.statValue}>{storyOverview?.foreshadowing?.length ?? 0}</div>
            <div className={styles.statLabel}>Foreshadowing</div>
          </div>
        </div>

        <div className={styles.pipeline}>
          <div className={styles.pipelineHeader}>
            <p className={styles.pipelineLabel}>Story Pipeline</p>
            <span>{state.pipelinePhase ?? 'world setup'}</span>
          </div>
          <div className={styles.pipelineBar} aria-hidden="true">
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

        {totalUsableAgents === 0 && (
          <section className={styles.emptyState} aria-label="World next steps">
            <div className={styles.emptyCopy}>
              <h2>Choose an agent lane to start writing</h2>
              <p>
                Merak is ready to hold the world, but it needs at least one voice before the
                workbench can become a live writing room.
              </p>
              <button
                type="button"
                className={styles.primaryStart}
                onClick={() => setShowCreateAgent(true)}
              >
                <UserRoundPlus size={16} aria-hidden="true" />
                Create first character
              </button>
            </div>
            <div className={styles.nextSteps}>
              <button
                type="button"
                className={`${styles.nextStep} ${styles.nextStepReady}`}
                onClick={() => setShowCreateAgent(true)}
              >
                <UserRoundPlus size={16} aria-hidden="true" />
                <span>
                  <strong>Create a character</strong>
                  <small>Unlocks the workbench and agent sessions.</small>
                </span>
              </button>
              <button
                type="button"
                className={styles.nextStep}
                onClick={() => setShowCreateAgent(true)}
              >
                <BookOpen size={16} aria-hidden="true" />
                <span>
                  <strong>Outline the first chapter</strong>
                  <small>Create a voice first, then shape chapter beats.</small>
                </span>
              </button>
              <button
                type="button"
                className={styles.nextStep}
                onClick={() => setShowCreateAgent(true)}
              >
                <PenLine size={16} aria-hidden="true" />
                <span>
                  <strong>Draft an opening scene</strong>
                  <small>Scene drafting starts once a character lane exists.</small>
                </span>
              </button>
            </div>
          </section>
        )}

        {godAgents.length > 0 && (
          <div className={styles.section}>
            <h2 className={styles.sectionTitle}>Creator</h2>
            <div className={styles.agentGrid}>
              {godAgents.map((agent) => (
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

        {showCreateAgent && state.worldId && (
          <CreateAgentModal
            worldId={state.worldId}
            onClose={() => setShowCreateAgent(false)}
            onCreated={handleAgentCreated}
          />
        )}
      </div>
    </div>
  );
}
