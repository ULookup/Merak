import { KeyRound, Loader2, ShieldAlert, Users, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from './CreateModal.module.css';

interface Props {
  worldId: string;
  onClose: () => void;
  onCreated?: () => void | Promise<void>;
}

export default function CreateSecretModal({ worldId, onClose, onCreated }: Props) {
  const { state } = useAppState();
  const [title, setTitle] = useState('');
  const [truth, setTruth] = useState('');
  const [publicVersion, setPublicVersion] = useState('');
  const [stakes, setStakes] = useState('');
  const [awareIds, setAwareIds] = useState('');
  const [suspiciousIds, setSuspiciousIds] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    function onKey(e: KeyboardEvent) { if (e.key === 'Escape' && !submitting) onClose(); }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose, submitting]);

  async function handleSubmit() {
    if (!title.trim() && !truth.trim()) return;
    setSubmitting(true);
    setError(null);
    try {
      await api.createSecret(worldId, {
        title: title.trim() || undefined,
        truth: truth.trim() || undefined,
        public_version: publicVersion.trim() || undefined,
        stakes: stakes.trim() || undefined,
        aware_character_ids: awareIds ? awareIds.split(',').map(t => t.trim()).filter(Boolean) : undefined,
        suspicious_character_ids: suspiciousIds ? suspiciousIds.split(',').map(t => t.trim()).filter(Boolean) : undefined,
        session_id: state.sessionId,
      });
      await onCreated?.();
      onClose();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <div className={styles.scrim} role="presentation">
      <section className={styles.modal} role="dialog" aria-modal="true" aria-label="创建秘密">
        <button className={styles.closeBtn} onClick={onClose} aria-label="取消" disabled={submitting}>
          <X size={17} aria-hidden="true" strokeWidth={2.4} />
        </button>
        <div className={styles.iconWrap}>
          <KeyRound size={28} aria-hidden="true" strokeWidth={2.1} />
        </div>
        <div className={styles.kicker}>知识边界</div>
        <h2>创建秘密</h2>
        <p>定义“真实发生了什么”和“角色分别知道什么”。这些边界会交给后端保存，用来避免角色知道不该知道的信息。</p>

        <label className={styles.field}>
          <span>标题</span>
          <input className={styles.input} value={title} onChange={(e) => setTitle(e.target.value)} placeholder="例如：王子的真实血统" />
        </label>

        <label className={styles.field}>
          <span>真相</span>
          <textarea className={styles.textarea} value={truth} onChange={(e) => setTruth(e.target.value)} placeholder="真实发生了什么？只有哪些人知道？" rows={3} />
        </label>

        <label className={styles.field}>
          <span>公开说法</span>
          <input className={styles.input} value={publicVersion} onChange={(e) => setPublicVersion(e.target.value)} placeholder="大多数角色目前相信的版本。" />
        </label>

        <label className={styles.field}>
          <span>风险</span>
          <input className={styles.input} value={stakes} onChange={(e) => setStakes(e.target.value)} placeholder="如果秘密暴露，会改变什么关系或局势？" />
        </label>

        <div className={styles.row}>
          <label className={styles.field}>
            <span><Users size={12} aria-hidden="true" /> 知情角色 ID</span>
            <input className={styles.input} value={awareIds} onChange={(e) => setAwareIds(e.target.value)} placeholder="多个角色 ID 用逗号分隔" />
          </label>
          <label className={styles.field}>
            <span><ShieldAlert size={12} aria-hidden="true" /> 怀疑中的角色 ID</span>
            <input className={styles.input} value={suspiciousIds} onChange={(e) => setSuspiciousIds(e.target.value)} placeholder="多个角色 ID 用逗号分隔" />
          </label>
        </div>

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.secondary} onClick={onClose} disabled={submitting}>取消</button>
          <button className={styles.primary} onClick={handleSubmit} disabled={submitting || (!title.trim() && !truth.trim())}>
            {submitting ? <><Loader2 size={15} aria-hidden="true" className={styles.spin} /> 正在创建...</> : '创建秘密'}
          </button>
        </div>
      </section>
    </div>
  );
}
