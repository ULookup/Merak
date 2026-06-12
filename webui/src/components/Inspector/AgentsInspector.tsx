import { useState } from 'react';
import { Brain, Fingerprint, Plus, Shield, UserPlus, Users } from 'lucide-react';
import { useAppState } from '../../AppState';
import AgentAvatar from '../AgentAvatar';
import styles from '../InspectorPanel.module.css';
import AgentCardView from './AgentCardView';
import AgentPromptViewer from './AgentPromptViewer';
import CreateAgentModal from './CreateAgentModal';

const kindLabels: Record<string, string> = {
  god: 'God',
  map_manager: 'Manager',
  history_manager: 'Manager',
  magic_system_manager: 'Manager',
  faction_manager: 'Manager',
  individual: 'Character',
  group: 'Group',
};

function groupKey(kind: string) {
  if (kind === 'god') return 'God';
  if (kind.includes('manager')) return 'Managers';
  if (kind === 'group') return 'Groups';
  return 'Characters';
}

export default function AgentsInspector() {
  const { state, dispatch } = useAppState();
  const [selectedAgentId, setSelectedAgentId] = useState<string | null>(null);
  const [showCreateAgent, setShowCreateAgent] = useState(false);
  const [promptAgentId, setPromptAgentId] = useState<string | null>(null);

  if (selectedAgentId) {
    return (
      <AgentCardView
        agentId={selectedAgentId}
        onClose={() => setSelectedAgentId(null)}
        onViewPrompt={() => setPromptAgentId(selectedAgentId)}
      />
    );
  }

  if (state.agents.length === 0) {
    return (
      <>
        <section className={styles.section}>
          <div className={styles.sectionTitle}>Character Voices</div>
          <p className={styles.muted}>No agents loaded for this world.</p>
          {state.worldId && (
            <button
              className={styles.entryButton}
              style={{ marginTop: 12 }}
              onClick={() => setShowCreateAgent(true)}
            >
              <UserPlus size={14} aria-hidden="true" />
              Create First Character
            </button>
          )}
        </section>
        {showCreateAgent && state.worldId && (
          <CreateAgentModal
            worldId={state.worldId}
            onClose={() => setShowCreateAgent(false)}
            onCreated={() => dispatch({ type: 'SET_STORY_VERSION' })}
          />
        )}
      </>
    );
  }

  const groups = state.agents.reduce<Record<string, typeof state.agents>>((acc, agent) => {
    const key = groupKey(agent.kind);
    acc[key] = [...(acc[key] ?? []), agent];
    return acc;
  }, {});

  const promptAgent = promptAgentId ? state.agents.find((a) => a.id === promptAgentId) : null;

  return (
    <>
      <section className={styles.runCard}>
        <span className={styles.pulse}>
          <Users size={14} aria-hidden="true" strokeWidth={2.4} />
        </span>
        <div>
          <div className={styles.sectionTitle}>Voice System</div>
          <strong>{state.agents.length} active agents</strong>
          <p>Grouped by narrative responsibility for fast delegation decisions.</p>
        </div>
      </section>

      <div style={{ display: 'flex', gap: 8 }}>
        {state.worldId && (
          <button className={styles.ghostButton} onClick={() => setShowCreateAgent(true)}>
            <Plus size={14} aria-hidden="true" />
            New Character
          </button>
        )}
      </div>

      {Object.entries(groups).map(([group, agents]) => (
        <section className={styles.section} key={group}>
          <div className={styles.sectionTitle}>{group}</div>
          {agents.map((agent) => (
            <div className={styles.agentRow} key={agent.id}>
              <div
                className={styles.agent}
                onClick={() => setSelectedAgentId(agent.id)}
                role="button"
                tabIndex={0}
                onKeyDown={(e) => {
                  if (e.key === 'Enter') setSelectedAgentId(agent.id);
                }}
              >
                <AgentAvatar
                  name={agent.display_name || agent.name}
                  src={agent.avatar_url}
                  size="sm"
                  className={styles.avatar}
                />
                <div>
                  <strong>{agent.display_name || agent.name}</strong>
                  <span>{kindLabels[agent.kind] ?? agent.kind}</span>
                </div>
              </div>
              <button
                className={styles.addBtn}
                onClick={(e) => {
                  e.stopPropagation();
                  setPromptAgentId(agent.id);
                }}
                aria-label={`View prompt for ${agent.display_name || agent.name}`}
                title="View system prompt"
              >
                <Brain size={12} aria-hidden="true" />
              </button>
            </div>
          ))}
        </section>
      ))}

      <section className={styles.section}>
        <div className={styles.sectionTitle}>Voice Diagnostics</div>
        <div className={styles.signalGrid}>
          <span>
            <Fingerprint size={14} aria-hidden="true" />
            Fingerprints pending
          </span>
          <span>
            <Shield size={14} aria-hidden="true" />
            Knowledge checks pending
          </span>
          <span>
            <Brain size={14} aria-hidden="true" />
            Prompt preview ready
          </span>
        </div>
      </section>

      {/* TODO: MemorySummary display — when the backend serves per-agent memory
          summaries via GET /api/worldbuilding/:worldId/agents/:agentId/memory-summaries,
          render MemorySummary data here (period_start, period_end, summary text,
          source diary count). Use the MemorySummary interface from ../../api/types. */}

      {showCreateAgent && state.worldId && (
        <CreateAgentModal
          worldId={state.worldId}
          onClose={() => setShowCreateAgent(false)}
          onCreated={() => dispatch({ type: 'SET_STORY_VERSION' })}
        />
      )}
      {promptAgent && (
        <AgentPromptViewer
          agentId={promptAgent.id}
          agentName={promptAgent.display_name || promptAgent.name}
          onClose={() => setPromptAgentId(null)}
        />
      )}
    </>
  );
}
