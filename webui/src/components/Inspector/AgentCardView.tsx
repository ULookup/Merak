import { useCallback, useEffect, useRef, useState } from 'react';
import { ArrowLeft, Pencil } from 'lucide-react';
import { api } from '../../api/client';
import type { AgentDetail } from '../../api/types';
import AgentAvatar from '../AgentAvatar';
import AgentCardEdit from './AgentCardEdit';
import styles from './AgentCardView.module.css';
import AgentImageGallery from './AgentImageGallery';

interface Props {
  worldId: string;
  agentId: string;
  onClose: () => void;
  onViewPrompt?: () => void;
}

export default function AgentCardView({ worldId, agentId, onClose, onViewPrompt }: Props) {
  const [detail, setDetail] = useState<AgentDetail | null>(null);
  const [editMode, setEditMode] = useState(false);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const abortRef = useRef<AbortController | undefined>(undefined);

  const load = useCallback(async () => {
    abortRef.current?.abort();
    const controller = new AbortController();
    abortRef.current = controller;
    setLoading(true);
    setError(null);
    try {
      const res = await api.fetchAgentDetail(worldId, agentId);
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
  }, [agentId, worldId]);

  useEffect(() => {
    load();
    return () => abortRef.current?.abort();
  }, [load]);

  if (loading) {
    return (
      <div className={styles.container}>
        <div className={styles.skeletonHeader} />
        <div className={styles.skeletonLine} />
        <div className={styles.skeletonLine} style={{ width: '70%' }} />
        <div className={styles.skeletonLine} style={{ width: '50%' }} />
        <div className={styles.skeletonLine} style={{ width: '80%' }} />
      </div>
    );
  }

  if (error) return <div className={styles.container}>Error: {error}</div>;
  if (!detail) return <div className={styles.container}>Agent not found</div>;

  if (editMode) {
    return (
      <AgentCardEdit
        worldId={worldId}
        agentId={agentId}
        detail={detail}
        onSave={(updated) => {
          setDetail(updated);
          setEditMode(false);
        }}
        onCancel={() => setEditMode(false)}
      />
    );
  }

  const cc = detail.character_card;
  const displayName = detail.display_name || detail.name;
  const images = detail.images ?? { avatar: [], design: [] };

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <button onClick={onClose} className={styles.backBtn}>
          <ArrowLeft size={14} aria-hidden="true" />
          Back
        </button>
        <div className={styles.headerActions}>
          {onViewPrompt && (
            <button onClick={onViewPrompt} className={styles.promptBtn}>
              Prompt
            </button>
          )}
          <button onClick={() => setEditMode(true)} className={styles.editBtn}>
            <Pencil size={13} aria-hidden="true" />
            Edit
          </button>
        </div>
      </div>

      <section className={styles.profileHero}>
        <AgentAvatar name={displayName} src={detail.avatar_url} size="lg" />
        <div className={styles.profileText}>
          <h3>{displayName}</h3>
          <span>{detail.kind}</span>
          {cc.identity || cc.appearance ? <p>{cc.identity || cc.appearance}</p> : null}
        </div>
      </section>

      <section className={styles.section}>
        <AgentImageGallery
          worldId={worldId}
          agentId={agentId}
          imageType="avatar"
          images={images.avatar}
          onChanged={load}
        />
      </section>

      <section className={styles.section}>
        <AgentImageGallery
          worldId={worldId}
          agentId={agentId}
          imageType="design"
          images={images.design}
          onChanged={load}
        />
      </section>

      {cc.age !== undefined || cc.gender || cc.race || cc.identity ? (
        <section className={styles.section}>
          <h4>Basics</h4>
          <div className={styles.fieldRow}>
            {cc.age !== undefined ? <span>Age: {cc.age}</span> : null}
            {cc.gender ? <span>Gender: {cc.gender}</span> : null}
            {cc.race ? <span>Race: {cc.race}</span> : null}
            {cc.identity ? <span>Identity: {cc.identity}</span> : null}
          </div>
        </section>
      ) : null}

      {cc.core_traits?.length ? (
        <section className={styles.section}>
          <h4>Core Traits</h4>
          <div className={styles.tags}>
            {cc.core_traits.map((trait) => (
              <span key={trait} className={styles.tag}>
                {trait}
              </span>
            ))}
          </div>
        </section>
      ) : null}

      {cc.emotional_tendency && (
        <section className={styles.section}>
          <h4>Emotional Tendency</h4>
          <p>{cc.emotional_tendency}</p>
        </section>
      )}

      {cc.speaking_style ? (
        <section className={styles.section}>
          <h4>Speaking Style</h4>
          <p>{cc.speaking_style}</p>
        </section>
      ) : null}

      {cc.core_desire ? (
        <section className={styles.section}>
          <h4>Core Desire</h4>
          <p>{cc.core_desire}</p>
        </section>
      ) : null}

      {cc.deep_fear ? (
        <section className={styles.section}>
          <h4>Deep Fear</h4>
          <p>{cc.deep_fear}</p>
        </section>
      ) : null}

      {cc.daily_goal ? (
        <section className={styles.section}>
          <h4>Daily Goal</h4>
          <p>{cc.daily_goal}</p>
        </section>
      ) : null}

      {cc.background && (
        <section className={styles.section}>
          <h4>Background</h4>
          <p className={styles.longText}>{cc.background}</p>
        </section>
      )}

      {cc.knowledge_scope ? (
        <section className={styles.section}>
          <h4>Knowledge Scope</h4>
          <p>{cc.knowledge_scope}</p>
        </section>
      ) : null}

      {cc.appearance && (
        <section className={styles.section}>
          <h4>Appearance</h4>
          <p>{cc.appearance}</p>
        </section>
      )}
    </div>
  );
}
