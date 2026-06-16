import { GitBranch, KeyRound, Loader2, Plus, UsersRound } from 'lucide-react';
import { useEffect, useMemo, useRef, useState } from 'react';
import { useAppState } from '../../AppState';
import { api } from '../../api/client';
import type { ForeshadowingItem, RelationEntry, SecretItem } from '../../api/types';
import CreateAgentModal from './CreateAgentModal';
import CreateForeshadowingModal from './CreateForeshadowingModal';
import CreateSecretModal from './CreateSecretModal';
import styles from './CreationDashboard.module.css';

type DashTab = 'foreshadow' | 'secrets' | 'relations';
type PanelIcon = typeof GitBranch;

interface GraphLink {
  source: string;
  target: string;
  kind: string;
}

const TABS: Array<{
  id: DashTab;
  label: string;
  description: string;
  icon: PanelIcon;
}> = [
  {
    id: 'foreshadow',
    label: 'Foreshadowing',
    description: 'Plant and track narrative threads.',
    icon: GitBranch,
  },
  {
    id: 'secrets',
    label: 'Secrets',
    description: 'Manage private truths and knowledge boundaries.',
    icon: KeyRound,
  },
  {
    id: 'relations',
    label: 'Relations',
    description: 'Map character ties, alliances, and tension.',
    icon: UsersRound,
  },
];

export default function CreationDashboard() {
  const { state, dispatch } = useAppState();
  const [tab, setTab] = useState<DashTab>('foreshadow');
  const [relations, setRelations] = useState<RelationEntry[]>([]);
  const [relationsLoading, setRelationsLoading] = useState(false);

  const [showCreateForeshadowing, setShowCreateForeshadowing] = useState(false);
  const [showCreateSecret, setShowCreateSecret] = useState(false);
  const [showCreateAgent, setShowCreateAgent] = useState(false);

  function handleCreated() {
    dispatch({ type: 'SET_STORY_VERSION' });
  }

  const activeTab = TABS.find((item) => item.id === tab) ?? TABS[0];
  const ActiveIcon = activeTab.icon;

  const foreshadowGroups = useMemo(() => {
    const open: ForeshadowingItem[] = [];
    const paid: ForeshadowingItem[] = [];
    const abandoned: ForeshadowingItem[] = [];
    state.foreshadowing.forEach((item) => {
      const status = item.status ?? 'open';
      if (status === 'paid') paid.push(item);
      else if (status === 'abandoned') abandoned.push(item);
      else open.push(item);
    });
    return { open, paid, abandoned };
  }, [state.foreshadowing]);

  const relationsCacheRef = useRef<{ key: string; relations: RelationEntry[] }>({
    key: '',
    relations: [],
  });

  useEffect(() => {
    if (!state.worldId || !state.agents.length) {
      setRelations([]);
      setRelationsLoading(false);
      return;
    }

    const key = `${state.worldId}:${state.agents.map((agent) => agent.id).sort().join(',')}`;
    if (relationsCacheRef.current.key === key) {
      setRelations(relationsCacheRef.current.relations);
      return;
    }

    setRelationsLoading(true);
    Promise.all(
      state.agents.map((agent) =>
        api.fetchRelations(state.worldId!, agent.id)
          .then((result) => result.relations)
          .catch(() => [] as RelationEntry[]),
      ),
    )
      .then((results) => {
        const flat = results.flat();
        relationsCacheRef.current = { key, relations: flat };
        setRelations(flat);
      })
      .catch(() => setRelations([]))
      .finally(() => setRelationsLoading(false));
  }, [state.worldId, state.agents]);

  const graphData = useMemo(() => {
    const nodes = state.agents.map((agent) => ({
      id: agent.id,
      label: (agent.display_name || agent.name).slice(0, 6),
    }));
    const seen = new Set<string>();
    const links: GraphLink[] = [];
    relations.forEach((relation) => {
      const key = [relation.agent_id, relation.target_id].sort().join('--');
      if (!seen.has(key)) {
        seen.add(key);
        links.push({
          source: relation.agent_id,
          target: relation.target_id,
          kind: relation.relation_type,
        });
      }
    });
    return { nodes, links };
  }, [state.agents, relations]);

  return (
    <div className={styles.container}>
      <div className={styles.tabs} role="tablist" aria-label="Creation dashboard sections">
        {TABS.map((item) => {
          const Icon = item.icon;
          const count =
            item.id === 'foreshadow'
              ? state.foreshadowing.length
              : item.id === 'secrets'
                ? state.secrets.length
                : state.agents.length;

          return (
            <button
              key={item.id}
              className={`${styles.tab} ${tab === item.id ? styles.active : ''}`}
              onClick={() => setTab(item.id)}
              type="button"
              role="tab"
              aria-selected={tab === item.id}
            >
              <Icon size={14} aria-hidden="true" strokeWidth={2.2} />
              <span>{item.label}</span>
              <strong>{count}</strong>
            </button>
          );
        })}
      </div>

      <div className={styles.toolbar}>
        <div className={styles.toolbarCopy}>
          <span className={styles.toolbarIcon} aria-hidden="true">
            <ActiveIcon size={15} strokeWidth={2.2} />
          </span>
          <div>
            <strong>{activeTab.label}</strong>
            <span>{activeTab.description}</span>
          </div>
        </div>
        {tab === 'foreshadow' && state.worldId && (
          <button
            className={styles.createBtn}
            onClick={() => setShowCreateForeshadowing(true)}
            type="button"
          >
            <Plus size={14} aria-hidden="true" />
            Plant Thread
          </button>
        )}
        {tab === 'secrets' && state.worldId && (
          <button
            className={styles.createBtn}
            onClick={() => setShowCreateSecret(true)}
            type="button"
          >
            <Plus size={14} aria-hidden="true" />
            Create Secret
          </button>
        )}
        {tab === 'relations' && state.worldId && (
          <button className={styles.createBtn} onClick={() => setShowCreateAgent(true)} type="button">
            <Plus size={14} aria-hidden="true" />
            Create Character
          </button>
        )}
      </div>

      <div className={styles.content}>
        {tab === 'foreshadow' && (
          <div className={styles.kanban}>
            <Column title="Open Threads" items={foreshadowGroups.open} color="open" />
            <Column title="Paid Off" items={foreshadowGroups.paid} color="paid" />
            <Column title="Retired" items={foreshadowGroups.abandoned} color="abandoned" />
          </div>
        )}

        {tab === 'secrets' && (
          <div className={styles.secretList}>
            {state.secrets.length === 0 && (
              <EmptyPanel
                icon={KeyRound}
                title="No secrets yet"
                body="Create a private truth, public version, or knowledge boundary for the story."
              />
            )}
            {state.secrets.map((secret) => (
              <SecretCard key={secret.id} secret={secret} agents={state.agents} />
            ))}
          </div>
        )}

        {tab === 'relations' && (
          <div className={styles.relationView}>
            {relationsLoading ? (
              <div className={styles.loading}>
                <Loader2 size={16} aria-hidden="true" className={styles.spin} />
                Loading relationships...
              </div>
            ) : (
              <>
                <RelationGraph agents={state.agents} links={graphData.links} />
                <div className={styles.relationList}>
                  {state.agents.map((agent) => (
                    <div key={agent.id} className={styles.relationAgent}>
                      <span className={styles.relAvatar}>
                        {(agent.display_name || agent.name).slice(0, 1)}
                      </span>
                      <div>
                        <strong>{agent.display_name || agent.name}</strong>
                        <span className={styles.relKind}>{agent.kind || 'character'}</span>
                      </div>
                    </div>
                  ))}
                  {state.agents.length === 0 && (
                    <EmptyPanel
                      icon={UsersRound}
                      title="No characters yet"
                      body="Create a character to begin mapping relationships and tensions."
                    />
                  )}
                  {state.agents.length > 0 && relations.length === 0 && (
                    <p className={styles.inlineHint}>No explicit relationship edges yet.</p>
                  )}
                </div>
              </>
            )}
          </div>
        )}
      </div>

      {showCreateForeshadowing && state.worldId && (
        <CreateForeshadowingModal
          worldId={state.worldId}
          onClose={() => setShowCreateForeshadowing(false)}
          onCreated={handleCreated}
        />
      )}
      {showCreateSecret && state.worldId && (
        <CreateSecretModal
          worldId={state.worldId}
          onClose={() => setShowCreateSecret(false)}
          onCreated={handleCreated}
        />
      )}
      {showCreateAgent && state.worldId && (
        <CreateAgentModal
          worldId={state.worldId}
          onClose={() => setShowCreateAgent(false)}
          onCreated={handleCreated}
        />
      )}
    </div>
  );
}

function Column({
  title,
  items,
  color,
}: {
  title: string;
  items: ForeshadowingItem[];
  color: 'open' | 'paid' | 'abandoned';
}) {
  return (
    <div className={`${styles.column} ${styles[color]}`}>
      <div className={styles.columnHeader}>
        {title} <span className={styles.count}>{items.length}</span>
      </div>
      <div className={styles.columnBody}>
        {items.map((item) => (
          <div key={item.id} className={styles.card}>
            <p className={styles.cardContent}>{item.content}</p>
            {item.pay_off_idea && <p className={styles.cardIdea}>{item.pay_off_idea}</p>}
            <div className={styles.cardMeta}>
              {item.hint_level && <span className={styles.hint}>{item.hint_level}</span>}
              {item.tags?.map((tag) => (
                <span key={tag} className={styles.tag}>
                  {tag}
                </span>
              ))}
            </div>
          </div>
        ))}
        {items.length === 0 && <p className={styles.colEmpty}>No items</p>}
      </div>
    </div>
  );
}

function EmptyPanel({ icon: Icon, title, body }: { icon: PanelIcon; title: string; body: string }) {
  return (
    <div className={styles.empty}>
      <Icon size={18} aria-hidden="true" strokeWidth={2.1} />
      <strong>{title}</strong>
      <span>{body}</span>
    </div>
  );
}

function SecretCard({
  secret,
  agents,
}: {
  secret: SecretItem;
  agents: { id: string; display_name: string; name: string }[];
}) {
  const awareNames = (secret.aware_character_ids ?? [])
    .map((id) => agents.find((agent) => agent.id === id)?.display_name || agents.find((agent) => agent.id === id)?.name || id)
    .join(', ');
  const suspiciousNames = (secret.suspicious_character_ids ?? [])
    .map((id) => agents.find((agent) => agent.id === id)?.display_name || agents.find((agent) => agent.id === id)?.name || id)
    .join(', ');

  return (
    <div className={`${styles.secretCard} ${styles[`secret_${secret.status || 'active'}`]}`}>
      <div className={styles.secretHeader}>
        <strong>{secret.title || 'Untitled secret'}</strong>
        <span className={styles.secretStatus}>{secret.status || 'active'}</span>
      </div>
      {secret.truth && <p className={styles.secretTruth}>{secret.truth}</p>}
      {secret.public_version && (
        <p className={styles.secretPublic}>Public version: {secret.public_version}</p>
      )}
      {secret.stakes && <p className={styles.secretStakes}>Stakes: {secret.stakes}</p>}
      {awareNames && <p className={styles.secretAware}>Aware: {awareNames}</p>}
      {suspiciousNames && <p className={styles.secretSuspicious}>Suspicious: {suspiciousNames}</p>}
    </div>
  );
}

function RelationGraph({
  agents,
  links,
}: {
  agents: { id: string; display_name: string; name: string }[];
  links: { source: string; target: string; kind: string }[];
}) {
  const n = agents.length;
  if (n === 0) return null;

  const cx = 140;
  const cy = 120;
  const r = 90;
  const nodeMap = new Map<string, { x: number; y: number }>();
  const nodes = agents.map((agent, index) => {
    const angle = (2 * Math.PI * index) / n - Math.PI / 2;
    const pos = { x: cx + r * Math.cos(angle), y: cy + r * Math.sin(angle) };
    nodeMap.set(agent.id, pos);
    return { id: agent.id, label: (agent.display_name || agent.name).slice(0, 6), ...pos };
  });

  const edgeLines = links
    .filter((link) => nodeMap.has(link.source) && nodeMap.has(link.target))
    .map((link) => {
      const source = nodeMap.get(link.source)!;
      const target = nodeMap.get(link.target)!;
      return {
        key: `${link.source}-${link.target}`,
        x1: source.x,
        y1: source.y,
        x2: target.x,
        y2: target.y,
      };
    });

  return (
    <svg viewBox="0 0 280 240" className={styles.graph} aria-label="Character relationship map">
      {edgeLines.map((edge) => (
        <line
          key={edge.key}
          x1={edge.x1}
          y1={edge.y1}
          x2={edge.x2}
          y2={edge.y2}
          strokeWidth={1}
          strokeDasharray="4 2"
        />
      ))}
      {n > 2 && <circle cx={cx} cy={cy} r={3} fill="var(--muted)" />}
      {nodes.map((node) => (
        <g key={node.id}>
          <circle cx={node.x} cy={node.y} r={16} strokeWidth={1.5} />
          <text x={node.x} y={node.y + 4} textAnchor="middle" fontSize={9}>
            {node.label}
          </text>
        </g>
      ))}
    </svg>
  );
}
