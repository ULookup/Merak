import { useState } from 'react';
import { Brain, Fingerprint, Plus, Shield, UserPlus, Users } from 'lucide-react';
import { useAppState } from '../../AppState';
import AgentAvatar from '../AgentAvatar';
import styles from '../InspectorPanel.module.css';
import AgentCardView from './AgentCardView';
import AgentPromptViewer from './AgentPromptViewer';
import CreateAgentModal from './CreateAgentModal';

const kindLabels: Record<string, string> = {
  god: '总控助手',
  map_manager: '设定助手',
  history_manager: '设定助手',
  magic_system_manager: '设定助手',
  faction_manager: '设定助手',
  individual: '角色',
  group: '群体',
};

function groupKey(kind: string) {
  if (kind === 'god') return '总控助手';
  if (kind.includes('manager')) return '设定助手';
  if (kind === 'group') return '群体';
  return '角色';
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
          <div className={styles.sectionTitle}>角色声音</div>
          <p className={styles.muted}>当前世界还没有角色。创建第一个角色后，工作台会用真实后端数据展示声音、提示词和知识边界。</p>
          {state.worldId && (
            <button
              className={styles.entryButton}
              style={{ marginTop: 12 }}
              onClick={() => setShowCreateAgent(true)}
            >
              <UserPlus size={14} aria-hidden="true" />
              创建第一个角色
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
          <div className={styles.sectionTitle}>声音系统</div>
          <strong>{state.agents.length} 个可用角色 / 助手</strong>
          <p>按叙事职责分组，方便判断该把任务交给哪个角色或工具助手。</p>
        </div>
      </section>

      <div style={{ display: 'flex', gap: 8 }}>
        {state.worldId && (
          <button className={styles.ghostButton} onClick={() => setShowCreateAgent(true)}>
            <Plus size={14} aria-hidden="true" />
            新建角色
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
                aria-label={`查看 ${agent.display_name || agent.name} 的系统提示词`}
                title="查看系统提示词"
              >
                <Brain size={12} aria-hidden="true" />
              </button>
            </div>
          ))}
        </section>
      ))}

      <section className={styles.section}>
        <div className={styles.sectionTitle}>声音诊断</div>
        <div className={styles.signalGrid}>
          <span>
            <Fingerprint size={14} aria-hidden="true" />
            指纹等待后端生成
          </span>
          <span>
            <Shield size={14} aria-hidden="true" />
            知识边界等待检查
          </span>
          <span>
            <Brain size={14} aria-hidden="true" />
            系统提示词可查看
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
