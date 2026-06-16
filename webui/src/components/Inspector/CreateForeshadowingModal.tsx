import { Eye, Lightbulb, Loader2, Tag, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from './CreateModal.module.css';

interface Props {
  worldId: string;
  onClose: () => void;
  onCreated?: () => void;
}

export default function CreateForeshadowingModal({ worldId, onClose, onCreated }: Props) {
  const { state } = useAppState();
  const [content, setContent] = useState('');
  const [payOffIdea, setPayOffIdea] = useState('');
  const [hintLevel, setHintLevel] = useState('visible');
  const [tags, setTags] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    function onKey(e: KeyboardEvent) { if (e.key === 'Escape' && !submitting) onClose(); }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose, submitting]);

  async function handleSubmit() {
    if (!content.trim()) return;
    setSubmitting(true);
    setError(null);
    try {
      await api.createForeshadowing(worldId, {
        content: content.trim(),
        pay_off_idea: payOffIdea.trim() || undefined,
        hint_level: hintLevel,
        tags: tags ? tags.split(',').map(t => t.trim()).filter(Boolean) : undefined,
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
      <section className={styles.modal} role="dialog" aria-modal="true" aria-label="埋设伏笔">
        <button className={styles.closeBtn} onClick={onClose} aria-label="取消" disabled={submitting}>
          <X size={17} aria-hidden="true" strokeWidth={2.4} />
        </button>
        <div className={styles.iconWrap}>
          <Lightbulb size={28} aria-hidden="true" strokeWidth={2.1} />
        </div>
        <div className={styles.kicker}>叙事线索</div>
        <h2>埋设伏笔</h2>
        <p>记录一个之后会回收的线索。这里不会生成内容，只把你写下的伏笔交给后端保存。</p>

        <label className={styles.field}>
          <span>伏笔内容</span>
          <textarea
            className={styles.textarea}
            value={content}
            onChange={(e) => setContent(e.target.value)}
            placeholder="写下读者现在会看到的暗示或异常。"
            rows={4}
          />
        </label>

        <label className={styles.field}>
          <span>回收想法</span>
          <input
            className={styles.input}
            value={payOffIdea}
            onChange={(e) => setPayOffIdea(e.target.value)}
            placeholder="之后可能如何解释、反转或兑现？"
          />
        </label>

        <div className={styles.row}>
          <label className={styles.field}>
            <span><Eye size={12} aria-hidden="true" /> 隐蔽程度</span>
            <select className={styles.select} value={hintLevel} onChange={(e) => setHintLevel(e.target.value)}>
              <option value="visible">可见</option>
              <option value="subtle">含蓄</option>
              <option value="obvious">明显</option>
            </select>
          </label>
          <label className={styles.field}>
            <span><Tag size={12} aria-hidden="true" /> 标签</span>
            <input
              className={styles.input}
              value={tags}
              onChange={(e) => setTags(e.target.value)}
              placeholder="悬疑，角色，世界观"
            />
          </label>
        </div>

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.secondary} onClick={onClose} disabled={submitting}>取消</button>
          <button className={styles.primary} onClick={handleSubmit} disabled={submitting || !content.trim()}>
            {submitting ? <><Loader2 size={15} aria-hidden="true" className={styles.spin} /> 正在保存...</> : '埋设伏笔'}
          </button>
        </div>
      </section>
    </div>
  );
}
