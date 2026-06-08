import { useState, useEffect, useCallback, useRef } from 'react';
import { api } from '../../api/client';
import type { AgentDetail } from '../../api/types';
import { useAppState } from '../../AppState';
import AgentCardEdit from './AgentCardEdit';
import styles from './AgentCardView.module.css';

interface Props {
  agentId: string;
  onClose: () => void;
}

export default function AgentCardView({ agentId, onClose }: Props) {
  const { state } = useAppState();
  const [detail, setDetail] = useState<AgentDetail | null>(null);
  const [editMode, setEditMode] = useState(false);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const abortRef = useRef<AbortController | undefined>(undefined);

  const load = useCallback(async () => {
    if (!state.worldId) return;
    abortRef.current?.abort();
    const controller = new AbortController();
    abortRef.current = controller;
    setLoading(true);
    setError(null);
    try {
      const res = await api.fetchAgentDetail(state.worldId, agentId);
      if (!controller.signal.aborted) {
        setDetail(res.agent);
        setLoading(false);
      }
    } catch (e) {
      if (!controller.signal.aborted) {
        setError((e as Error).message);
        setLoading(false);
      }
    }
  }, [agentId, state.worldId]);

  useEffect(() => {
    load();
    return () => abortRef.current?.abort();
  }, [load]);

  if (loading) return <div className={styles.container}>Loading...</div>;
  if (error) return <div className={styles.container}>Error: {error}</div>;
  if (!detail) return <div className={styles.container}>Agent not found</div>;

  if (editMode) {
    if (!state.worldId) return <div className={styles.container}>No world selected</div>;
    return (
      <AgentCardEdit
        worldId={state.worldId}
        agentId={agentId}
        detail={detail}
        onSave={(updated) => { setDetail(updated); setEditMode(false); }}
        onCancel={() => setEditMode(false)}
      />
    );
  }

  const cc = detail.character_card;
  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <button onClick={onClose} className={styles.backBtn}>← 返回</button>
        <h3>{detail.display_name || detail.name}</h3>
        <button onClick={() => setEditMode(true)} className={styles.editBtn}>编辑</button>
      </div>

      <section className={styles.section}>
        <h4>基础信息</h4>
        <div className={styles.fieldRow}>
          <span>年龄：{cc.age ?? '—'}</span>
          <span>性别：{cc.gender ?? '—'}</span>
          <span>种族：{cc.race ?? '—'}</span>
          <span>身份：{cc.identity ?? '—'}</span>
        </div>
      </section>

      <section className={styles.section}>
        <h4>性格特征</h4>
        <div className={styles.tags}>
          {cc.core_traits?.map(t => <span key={t} className={styles.tag}>{t}</span>)}
        </div>
      </section>

      {cc.emotional_tendency && (
        <section className={styles.section}>
          <h4>情感倾向</h4>
          <p>{cc.emotional_tendency}</p>
        </section>
      )}

      <section className={styles.section}>
        <h4>说话风格</h4>
        <p>{cc.speaking_style ?? '—'}</p>
      </section>

      <section className={styles.section}>
        <h4>核心欲望</h4>
        <p>{cc.core_desire ?? '—'}</p>
      </section>

      <section className={styles.section}>
        <h4>深层恐惧</h4>
        <p>{cc.deep_fear ?? '—'}</p>
      </section>

      <section className={styles.section}>
        <h4>日常目标</h4>
        <p>{cc.daily_goal ?? '—'}</p>
      </section>

      {cc.background && (
        <section className={styles.section}>
          <h4>背景故事</h4>
          <p className={styles.longText}>{cc.background}</p>
        </section>
      )}

      <section className={styles.section}>
        <h4>知识范围</h4>
        <p>{cc.knowledge_scope ?? '—'}</p>
      </section>

      {cc.appearance && (
        <section className={styles.section}>
          <h4>外观</h4>
          <p>{cc.appearance}</p>
        </section>
      )}
    </div>
  );
}
