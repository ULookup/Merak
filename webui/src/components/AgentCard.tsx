import { useState } from 'react';
import { api } from '../api/client';
import type { WorldAgent } from '../api/types';
import { useAppState } from '../AppState';
import AgentAvatar from './AgentAvatar';
import styles from './AgentCard.module.css';

interface AgentCardProps {
  agent: WorldAgent;
  worldId: string;
}

const AGENT_LABELS: Record<string, string> = {
  god: 'God (Omniscient)',
  map_manager: 'Map Manager',
  history_manager: 'History Manager',
  magic_system_manager: 'Magic System Manager',
  faction_manager: 'Faction Manager',
  individual: 'Character',
  group: 'Group',
};

export default function AgentCard({ agent, worldId }: AgentCardProps) {
  const { dispatch } = useAppState();
  const [entering, setEntering] = useState(false);

  const kind = agent.kind;
  const label = AGENT_LABELS[kind] ?? kind;
  const toolCount = kind === 'god' ? 20 : kind.includes('manager') ? 1 : 3;

  async function handleEnter() {
    setEntering(true);
    try {
      const res = await api.getOrCreateAgentSession(worldId, agent.id);
      dispatch({
        type: 'SET_AGENT_SESSION',
        sessionId: res.session.id,
        agentId: agent.id,
      });
    } catch {
      setEntering(false);
    }
  }

  return (
    <div
      className={styles.card}
      role="button"
      tabIndex={0}
      onKeyDown={(e) => {
        if (e.key === 'Enter') handleEnter();
      }}
    >
      <AgentAvatar
        name={agent.display_name || agent.name}
        src={agent.avatar_url}
        size="md"
        className={styles.icon}
      />
      <div className={styles.info}>
        <p className={styles.name}>{agent.display_name || agent.name}</p>
        <p className={styles.detail}>{label}</p>
        <p className={styles.toolCount}>{toolCount} tools</p>
      </div>
      <button className={styles.enterBtn} onClick={handleEnter} disabled={entering}>
        {entering ? '...' : 'Enter Chat'}
      </button>
    </div>
  );
}
