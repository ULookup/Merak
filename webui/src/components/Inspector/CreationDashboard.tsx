import { useState, useMemo, useEffect } from 'react';
import { useAppState } from '../../AppState';
import { api } from '../../api/client';
import type { ForeshadowingItem, SecretItem, RelationEntry } from '../../api/types';
import styles from './CreationDashboard.module.css';

type DashTab = 'foreshadow' | 'secrets' | 'relations';

interface GraphLink {
  source: string;
  target: string;
  kind: string;
}

export default function CreationDashboard() {
  const { state } = useAppState();
  const [tab, setTab] = useState<DashTab>('foreshadow');
  const [relations, setRelations] = useState<RelationEntry[]>([]);
  const [relationsLoading, setRelationsLoading] = useState(false);

  // Group foreshadowing by status
  const foreshadowGroups = useMemo(() => {
    const open: ForeshadowingItem[] = [];
    const paid: ForeshadowingItem[] = [];
    const abandoned: ForeshadowingItem[] = [];
    state.foreshadowing.forEach(f => {
      const s = f.status ?? 'open';
      if (s === 'paid') paid.push(f);
      else if (s === 'abandoned') abandoned.push(f);
      else open.push(f);
    });
    return { open, paid, abandoned };
  }, [state.foreshadowing]);

  // Fetch relations from API
  useEffect(() => {
    if (!state.worldId || !state.agents.length) {
      setRelations([]);
      return;
    }
    setRelationsLoading(true);
    Promise.all(state.agents.map(a =>
      api.fetchRelations(state.worldId!, a.id)
        .then(r => r.relations)
        .catch(() => [] as RelationEntry[])
    )).then(results => {
      setRelations(results.flat());
      setRelationsLoading(false);
    });
  }, [state.worldId, state.agents]);

  // Build relation graph data from real relations
  const graphData = useMemo(() => {
    const nodes = state.agents.map(a => ({
      id: a.id,
      label: (a.display_name || a.name).slice(0, 6),
    }));
    const seen = new Set<string>();
    const links: GraphLink[] = [];
    relations.forEach(r => {
      const key = [r.agent_id, r.target_id].sort().join('--');
      if (!seen.has(key)) {
        seen.add(key);
        links.push({ source: r.agent_id, target: r.target_id, kind: r.relation_type });
      }
    });
    return { nodes, links };
  }, [state.agents, relations]);

  return (
    <div className={styles.container}>
      <div className={styles.tabs}>
        <button
          className={`${styles.tab} ${tab === 'foreshadow' ? styles.active : ''}`}
          onClick={() => setTab('foreshadow')}
        >
          伏笔 ({state.foreshadowing.length})
        </button>
        <button
          className={`${styles.tab} ${tab === 'secrets' ? styles.active : ''}`}
          onClick={() => setTab('secrets')}
        >
          秘密 ({state.secrets.length})
        </button>
        <button
          className={`${styles.tab} ${tab === 'relations' ? styles.active : ''}`}
          onClick={() => setTab('relations')}
        >
          关系 ({state.agents.length})
        </button>
      </div>

      <div className={styles.content}>
        {tab === 'foreshadow' && (
          <div className={styles.kanban}>
            <Column title="待回收" items={foreshadowGroups.open} color="open" />
            <Column title="已回收" items={foreshadowGroups.paid} color="paid" />
            <Column title="已废弃" items={foreshadowGroups.abandoned} color="abandoned" />
          </div>
        )}

        {tab === 'secrets' && (
          <div className={styles.secretList}>
            {state.secrets.length === 0 && (
              <p className={styles.empty}>暂无秘密</p>
            )}
            {state.secrets.map(s => (
              <SecretCard key={s.id} secret={s} agents={state.agents} />
            ))}
          </div>
        )}

        {tab === 'relations' && (
          <div className={styles.relationView}>
            <RelationGraph agents={state.agents} links={graphData.links} />
            <div className={styles.relationList}>
              {state.agents.map(a => (
                <div key={a.id} className={styles.relationAgent}>
                  <span className={styles.relAvatar}>
                    {(a.display_name || a.name).slice(0, 1)}
                  </span>
                  <div>
                    <strong>{a.display_name || a.name}</strong>
                    <span className={styles.relKind}>{a.kind}</span>
                  </div>
                </div>
              ))}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}

function Column({ title, items, color }: { title: string; items: ForeshadowingItem[]; color: 'open' | 'paid' | 'abandoned' }) {
  return (
    <div className={`${styles.column} ${styles[color]}`}>
      <div className={styles.columnHeader}>
        {title} <span className={styles.count}>{items.length}</span>
      </div>
      <div className={styles.columnBody}>
        {items.map(item => (
          <div key={item.id} className={styles.card}>
            <p className={styles.cardContent}>{item.content}</p>
            {item.pay_off_idea && (
              <p className={styles.cardIdea}>{item.pay_off_idea}</p>
            )}
            <div className={styles.cardMeta}>
              {item.hint_level && <span className={styles.hint}>{item.hint_level}</span>}
              {item.tags?.map(t => <span key={t} className={styles.tag}>{t}</span>)}
            </div>
          </div>
        ))}
        {items.length === 0 && <p className={styles.colEmpty}>—</p>}
      </div>
    </div>
  );
}

function SecretCard({ secret, agents }: { secret: SecretItem; agents: { id: string; display_name: string; name: string }[] }) {
  const awareNames = (secret.aware_character_ids ?? [])
    .map(id => agents.find(a => a.id === id)?.display_name || agents.find(a => a.id === id)?.name || id)
    .join(', ');
  const suspiciousNames = (secret.suspicious_character_ids ?? [])
    .map(id => agents.find(a => a.id === id)?.display_name || agents.find(a => a.id === id)?.name || id)
    .join(', ');

  return (
    <div className={`${styles.secretCard} ${styles[`secret_${secret.status || 'active'}`]}`}>
      <div className={styles.secretHeader}>
        <strong>{secret.title || '未命名秘密'}</strong>
        <span className={styles.secretStatus}>{secret.status || 'active'}</span>
      </div>
      {secret.truth && <p className={styles.secretTruth}>{secret.truth}</p>}
      {secret.public_version && (
        <p className={styles.secretPublic}>公开版本：{secret.public_version}</p>
      )}
      {secret.stakes && <p className={styles.secretStakes}>利害：{secret.stakes}</p>}
      {awareNames && <p className={styles.secretAware}>知晓者：{awareNames}</p>}
      {suspiciousNames && <p className={styles.secretSuspicious}>怀疑者：{suspiciousNames}</p>}
    </div>
  );
}

function RelationGraph({ agents, links }: {
  agents: { id: string; display_name: string; name: string }[];
  links: { source: string; target: string; kind: string }[];
}) {
  const n = agents.length;
  if (n === 0) return <p className={styles.empty}>暂无角色关系数据</p>;

  const cx = 140, cy = 120, r = 90;
  const nodeMap = new Map<string, { x: number; y: number }>();
  const nodes = agents.map((a, i) => {
    const angle = (2 * Math.PI * i) / n - Math.PI / 2;
    const pos = { x: cx + r * Math.cos(angle), y: cy + r * Math.sin(angle) };
    nodeMap.set(a.id, pos);
    return { id: a.id, label: (a.display_name || a.name).slice(0, 6), ...pos };
  });

  const edgeLines = links
    .filter(l => nodeMap.has(l.source) && nodeMap.has(l.target))
    .map(l => {
      const s = nodeMap.get(l.source)!;
      const t = nodeMap.get(l.target)!;
      return { key: `${l.source}-${l.target}`, x1: s.x, y1: s.y, x2: t.x, y2: t.y };
    });

  return (
    <svg viewBox="0 0 280 240" className={styles.graph}>
      {edgeLines.map(e => (
        <line key={e.key} x1={e.x1} y1={e.y1} x2={e.x2} y2={e.y2}
              stroke="#4fc3f7" strokeWidth={1} strokeDasharray="4 2" />
      ))}
      {n > 2 && (
        <circle cx={cx} cy={cy} r={3} fill="#666" />
      )}
      {nodes.map(node => (
        <g key={node.id}>
          <circle cx={node.x} cy={node.y} r={16} fill="#2a2a4a" stroke="#4fc3f7" strokeWidth={1.5} />
          <text
            x={node.x}
            y={node.y + 4}
            textAnchor="middle"
            fill="#ddd"
            fontSize={9}
          >
            {node.label}
          </text>
        </g>
      ))}
    </svg>
  );
}
