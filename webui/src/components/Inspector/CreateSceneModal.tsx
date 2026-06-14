import { Clapperboard, Loader2, MapPin, Users, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from './CreateModal.module.css';

interface Props {
  worldId: string;
  onClose: () => void;
  onCreated?: () => void;
}

export default function CreateSceneModal({ worldId, onClose, onCreated }: Props) {
  const { state } = useAppState();
  const overview = state.storyOverview;
  const [title, setTitle] = useState('');
  const [chapterId, setChapterId] = useState(overview?.current_chapter?.id ?? '');
  const [worldTime, setWorldTime] = useState('');
  const [participantIds, setParticipantIds] = useState('');
  const [narrative, setNarrative] = useState('');
  const [sectionId, setSectionId] = useState('');
  const [locationId, setLocationId] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    function onKey(e: KeyboardEvent) { if (e.key === 'Escape' && !submitting) onClose(); }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose, submitting]);

  async function handleSubmit() {
    if (!title.trim() || !chapterId.trim()) return;
    setSubmitting(true);
    setError(null);
    try {
      await api.createScene(worldId, {
        title: title.trim(),
        chapter_id: chapterId.trim(),
        world_time: worldTime.trim() || undefined,
        participant_ids: participantIds ? participantIds.split(',').map(t => t.trim()).filter(Boolean) : undefined,
        narrative: narrative.trim() || undefined,
        section_id: sectionId.trim() || undefined,
        location_id: locationId.trim() || undefined,
        session_id: state.sessionId,
      });
      onCreated?.();
      onClose();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <div className={styles.scrim} role="presentation">
      <section className={styles.modal} role="dialog" aria-modal="true" aria-label="创建场景">
        <button className={styles.closeBtn} onClick={onClose} aria-label="取消" disabled={submitting}>
          <X size={17} aria-hidden="true" strokeWidth={2.4} />
        </button>
        <div className={styles.iconWrap}>
          <Clapperboard size={28} aria-hidden="true" strokeWidth={2.1} />
        </div>
        <div className={styles.kicker}>叙事片段</div>
        <h2>创建场景</h2>
        <p>把一个独立故事片段保存到当前世界。章节 ID 和角色 ID 会直接传给后端，不会在前端伪造关系。</p>

        <label className={styles.field}>
          <span>场景标题</span>
          <input className={styles.input} value={title} onChange={(e) => setTitle(e.target.value)} placeholder="例如：酒馆里的摊牌" />
        </label>

        <div className={styles.row}>
          <label className={styles.field}>
            <span>所属章节 ID</span>
            <input className={styles.input} value={chapterId} onChange={(e) => setChapterId(e.target.value)} placeholder="从当前章节自动带入，或粘贴后端返回的章节 ID" />
          </label>
          <label className={styles.field}>
            <span><MapPin size={12} aria-hidden="true" /> 世界时间</span>
            <input className={styles.input} value={worldTime} onChange={(e) => setWorldTime(e.target.value)} placeholder={state.worldTime ?? '例如：第一天清晨'} />
          </label>
        </div>

        <label className={styles.field}>
          <span><Users size={12} aria-hidden="true" /> 参与角色 ID</span>
          <input className={styles.input} value={participantIds} onChange={(e) => setParticipantIds(e.target.value)} placeholder="多个角色 ID 用逗号分隔" />
        </label>

        <div className={styles.row}>
          <label className={styles.field}>
            <span>分段 ID</span>
            <input className={styles.input} value={sectionId} onChange={(e) => setSectionId(e.target.value)} placeholder="可选，留空则由后端处理" />
          </label>
          <label className={styles.field}>
            <span>地点 ID</span>
            <input className={styles.input} value={locationId} onChange={(e) => setLocationId(e.target.value)} placeholder="可选，已有地点时再填写" />
          </label>
        </div>

        <label className={styles.field}>
          <span>开场正文</span>
          <textarea className={styles.textarea} value={narrative} onChange={(e) => setNarrative(e.target.value)} placeholder="可选：写一段场景开头，后端会保存为正文。" rows={3} />
        </label>

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.secondary} onClick={onClose} disabled={submitting}>取消</button>
          <button className={styles.primary} onClick={handleSubmit} disabled={submitting || !title.trim() || !chapterId.trim()}>
            {submitting ? <><Loader2 size={15} aria-hidden="true" className={styles.spin} /> 正在创建...</> : '创建场景'}
          </button>
        </div>
      </section>
    </div>
  );
}
