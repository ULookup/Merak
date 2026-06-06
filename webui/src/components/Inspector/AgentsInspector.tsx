import { Brain, Fingerprint, Shield, Users } from 'lucide-react';
import { useAppState } from '../../AppState';
import styles from '../InspectorPanel.module.css';

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
  const { state } = useAppState();
  const groups = state.agents.reduce<Record<string, typeof state.agents>>((acc, agent) => {
    const key = groupKey(agent.kind);
    acc[key] = [...(acc[key] ?? []), agent];
    return acc;
  }, {});

  if (state.agents.length === 0) {
    return (
      <section className={styles.section}>
        <div className={styles.sectionTitle}>Character Voices</div>
        <p className={styles.muted}>No agents loaded for this world.</p>
      </section>
    );
  }

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

      {Object.entries(groups).map(([group, agents]) => (
        <section className={styles.section} key={group}>
          <div className={styles.sectionTitle}>{group}</div>
          {agents.map((agent) => (
            <div className={styles.agent} key={agent.id}>
              <div className={styles.avatar}>{(agent.display_name || agent.name).slice(0, 1)}</div>
              <div>
                <strong>{agent.display_name || agent.name}</strong>
                <span>{kindLabels[agent.kind] ?? agent.kind}</span>
              </div>
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
    </>
  );
}
