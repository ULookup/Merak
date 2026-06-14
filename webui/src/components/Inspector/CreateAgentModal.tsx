import { Loader2, Sparkles, UserPlus, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from './CreateModal.module.css';

interface Props {
  worldId: string;
  onClose: () => void;
  onCreated?: () => void;
}

export default function CreateAgentModal({ worldId, onClose, onCreated }: Props) {
  const { state } = useAppState();
  const [name, setName] = useState('');
  const [identity, setIdentity] = useState('');
  const [gender, setGender] = useState('');
  const [age, setAge] = useState('');
  const [race, setRace] = useState('');
  const [coreDesire, setCoreDesire] = useState('');
  const [deepFear, setDeepFear] = useState('');
  const [speakingStyle, setSpeakingStyle] = useState('');
  const [background, setBackground] = useState('');
  const [traits, setTraits] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    function onKey(e: KeyboardEvent) { if (e.key === 'Escape' && !submitting) onClose(); }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose, submitting]);

  async function handleSubmit() {
    if (!name.trim()) return;
    setSubmitting(true);
    setError(null);
    try {
      await api.createAgent(worldId, {
        name: name.trim(),
        identity: identity.trim() || undefined,
        gender: gender.trim() || undefined,
        age: age ? parseInt(age, 10) : undefined,
        race: race.trim() || undefined,
        core_desire: coreDesire.trim() || undefined,
        deep_fear: deepFear.trim() || undefined,
        speaking_style: speakingStyle.trim() || undefined,
        background: background.trim() || undefined,
        core_traits: traits ? traits.split(',').map(t => t.trim()).filter(Boolean) : undefined,
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
      <section className={styles.modal} role="dialog" aria-modal="true" aria-label="创建角色">
        <button className={styles.closeBtn} onClick={onClose} aria-label="取消" disabled={submitting}>
          <X size={17} aria-hidden="true" strokeWidth={2.4} />
        </button>
        <div className={styles.iconWrap}>
          <UserPlus size={28} aria-hidden="true" strokeWidth={2.1} />
        </div>
        <div className={styles.kicker}>角色声音</div>
        <h2>创建角色</h2>
        <p>把角色的身份、动机和说话方式交给后端保存。信息越清楚，后续场景里的角色声音越稳定。</p>

        <div className={styles.row}>
          <label className={styles.field}>
            <span>角色名</span>
            <input className={styles.input} value={name} onChange={(e) => setName(e.target.value)} placeholder="例如：艾拉、陈泊舟" />
          </label>
          <label className={styles.field}>
            <span>身份定位</span>
            <input className={styles.input} value={identity} onChange={(e) => setIdentity(e.target.value)} placeholder="例如：边境医师、失势贵族" />
          </label>
        </div>

        <div className={styles.row}>
          <label className={styles.field}>
            <span>性别</span>
            <input className={styles.input} value={gender} onChange={(e) => setGender(e.target.value)} placeholder="可留空，或写角色自我认同" />
          </label>
          <label className={styles.field}>
            <span>年龄</span>
            <input className={styles.input} type="number" value={age} onChange={(e) => setAge(e.target.value)} placeholder="28" />
          </label>
        </div>

        <label className={styles.field}>
          <span>种族 / 群体</span>
          <input className={styles.input} value={race} onChange={(e) => setRace(e.target.value)} placeholder="例如：人类、仿生人、北境氏族" />
        </label>

        <label className={styles.field}>
          <span>核心欲望</span>
          <input className={styles.input} value={coreDesire} onChange={(e) => setCoreDesire(e.target.value)} placeholder="这个角色最想得到什么？" />
        </label>

        <label className={styles.field}>
          <span>深层恐惧</span>
          <input className={styles.input} value={deepFear} onChange={(e) => setDeepFear(e.target.value)} placeholder="他们最害怕失去或面对什么？" />
        </label>

        <label className={styles.field}>
          <span>说话风格</span>
          <input className={styles.input} value={speakingStyle} onChange={(e) => setSpeakingStyle(e.target.value)} placeholder="例如：克制、讽刺、短句多" />
        </label>

        <label className={styles.field}>
          <span>背景</span>
          <textarea className={styles.textarea} value={background} onChange={(e) => setBackground(e.target.value)} placeholder="简要写下经历、关系或创伤。" rows={3} />
        </label>

        <label className={styles.field}>
          <span>核心特质（用逗号分隔）</span>
          <input className={styles.input} value={traits} onChange={(e) => setTraits(e.target.value)} placeholder="勇敢，敏感，容易冲动" />
        </label>

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button className={styles.secondary} onClick={onClose} disabled={submitting}>取消</button>
          <button className={styles.primary} onClick={handleSubmit} disabled={submitting || !name.trim()}>
            {submitting ? <><Loader2 size={15} aria-hidden="true" className={styles.spin} /> 正在创建...</> : <><Sparkles size={14} aria-hidden="true" /> 创建角色</>}
          </button>
        </div>
      </section>
    </div>
  );
}
