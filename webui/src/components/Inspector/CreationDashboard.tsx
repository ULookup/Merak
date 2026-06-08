import { useState, useMemo } from 'react';
import { useAppState } from '../../AppState';
import type { ForeshadowingItem, SecretItem } from '../../api/types';
import styles from './CreationDashboard.module.css';

type DashTab = 'foreshadow' | 'secrets' | 'relations';

interface RelationLink {
  source: string;
  target: string;
  kind: string;
}

export default function CreationDashboard() {
  const { state } = useAppState();
  const [tab, setTab] = useState<DashTab>('foreshadow');

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

  // Build relation graph data
  const relationData = useMemo(() => {
    const nodes = state.agents.map(a => ({
      id: a.id,
      label: (a.display_name || a.name).slice(0, 6),
    }));
    const links: RelationLink[] = [];
    const linkSet = new Set<string>();
    state.foreshadowing.forEach(f => {
      if (f.tags) {
        f.tags.forEach(tag => {
          const agent = state.agents.find(a => a.name === tag || a.display_name === tag);
          if (agent && !linkSet.has(`${agent.id}-${tag}`)) {
            linkSet.add(`${agent.id}-${tag}`);
          }
        });
      }
    });
    return { nodes, links };
  }, [state.agents, state.foreshadowing]);

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
            <RelationGraph
              agents={state.agents}
            />
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

function Column({ title, items, color }: { title: string; items: ForeshadowingItem[]; color: string }) {
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

function RelationGraph({ agents }: { agents: { id: string; display_name: string; name: string }[] }) {
  const n = agents.length;
  if (n === 0) return <p className={styles.empty}>暂无角色关系数据</p>;

  const cx = 140, cy = 120, r = 90;
  const nodes = agents.map((a, i) => {
    const angle = (2 * Math.PI * i) / n - Math.PI / 2;
    return {
      id: a.id,
      label: (a.display_name || a.name).slice(0, 6),
      x: cx + r * Math.cos(angle),
      y: cy + r * Math.sin(angle),
    };
  });

  return (
    <svg viewBox="0 0 280 240" className={styles.graph}>
      {/* Edges: connect all nodes in a ring */}
      {nodes.map((node, i) => {
        const next = nodes[(i + 1) % n];
        return (
          <line
            key={`${node.id}-${next.id}`}
            x1={node.x} y1={node.y}
            x2={next.x} y2={next.y}
            stroke="#444"
            strokeWidth={1}
            strokeDasharray="4 2"
          />
        );
      })}
      {/* Center hub */}
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
