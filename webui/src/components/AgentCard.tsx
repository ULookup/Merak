import { useState } from 'react';
import { api } from '../api/client';
import type { WorldAgent } from '../api/types';
import { useAppState } from '../AppState';
import { useI18n } from '../i18n';
import AgentAvatar from './AgentAvatar';
import styles from './AgentCard.module.css';

interface AgentCardProps {
  agent: WorldAgent;
  worldId: string;
}

export default function AgentCard({ agent, worldId }: AgentCardProps) {
  const { t } = useI18n();
  const { dispatch } = useAppState();
  const [entering, setEntering] = useState(false);

  const kind = agent.kind;
  const labelKey = `agentKind.${kind}`;
  const label = t(labelKey) === labelKey ? kind : t(labelKey);
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
        <p className={styles.toolCount}>{t('agentCard.abilities').replace('{count}', String(toolCount))}</p>
      </div>
      <button className={styles.enterBtn} onClick={handleEnter} disabled={entering}>
        {entering ? '...' : t('agentCard.enter')}
      </button>
    </div>
  );
}
