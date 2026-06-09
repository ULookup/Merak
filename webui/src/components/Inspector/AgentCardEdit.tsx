import { useState } from 'react';
import { api, ApiError } from '../../api/client';
import type { AgentDetail } from '../../api/types';
import styles from './AgentCardEdit.module.css';

interface Props {
  worldId: string;
  agentId: string;
  detail: AgentDetail;
  onSave: (updated: AgentDetail) => void;
  onCancel: () => void;
}

export default function AgentCardEdit({ worldId, agentId, detail, onSave, onCancel }: Props) {
  const cc = detail.character_card;
  const [fields, setFields] = useState<Record<string, string>>({
    age: String(cc.age ?? ''),
    gender: cc.gender ?? '',
    race: cc.race ?? '',
    identity: cc.identity ?? '',
    core_traits: cc.core_traits?.join('、') ?? '',
    emotional_tendency: cc.emotional_tendency ?? '',
    speaking_style: cc.speaking_style ?? '',
    core_desire: cc.core_desire ?? '',
    deep_fear: cc.deep_fear ?? '',
    daily_goal: cc.daily_goal ?? '',
    background: cc.background ?? '',
    knowledge_scope: cc.knowledge_scope ?? '',
    appearance: cc.appearance ?? '',
    taboo_topics: cc.taboo_topics?.join('、') ?? '',
  });
  const [saving, setSaving] = useState(false);
  const [conflict, setConflict] = useState(false);

  const set = (key: string, value: string) => setFields(prev => ({ ...prev, [key]: value }));

  const handleSave = async () => {
    setSaving(true);
    setConflict(false);
    try {
      const payload: Record<string, unknown> = {};
      for (const [k, v] of Object.entries(fields)) {
        if (k === 'core_traits') {
          const traits = v.split(/[,，、\s]+/).filter(Boolean);
          if (traits.length > 0) payload[k] = traits;
        } else if (v !== '' && v !== String(cc[k as keyof typeof cc] ?? '')) {
          payload[k] = k === 'age' ? Number(v) : v;
        }
      }
      if (Object.keys(payload).length === 0) {
        onCancel();
        return;
      }
      const res = await api.patchAgentCard(worldId, agentId, payload, cc.version);
      onSave({ ...detail, character_card: { ...cc, ...payload, version: res.version } });
    } catch (e: unknown) {
      if (e instanceof ApiError && e.code === 'version_conflict') {
        setConflict(true);
      }
    } finally {
      setSaving(false);
    }
  };

  const field = (label: string, key: string, textarea = false) => (
    <label className={styles.field}>
      <span>{label}</span>
      {textarea ? (
        <textarea value={fields[key] ?? ''} onChange={e => set(key, e.target.value)} rows={4} />
      ) : (
        <input value={fields[key] ?? ''} onChange={e => set(key, e.target.value)} />
      )}
    </label>
  );

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <h3>编辑：{detail.display_name || detail.name}</h3>
      </div>
      {conflict && (
        <div className={styles.conflictBanner}>
          此卡片已被其他来源修改。请刷新后重新编辑。
        </div>
      )}
      <div className={styles.form}>
        {field('年龄', 'age')}
        {field('性别', 'gender')}
        {field('种族', 'race')}
        {field('身份', 'identity')}
        {field('性格特征（逗号或空格分隔）', 'core_traits')}
        {field('情感倾向', 'emotional_tendency')}
        {field('说话风格', 'speaking_style')}
        {field('核心欲望', 'core_desire')}
        {field('深层恐惧', 'deep_fear')}
        {field('日常目标', 'daily_goal')}
        {field('知识范围', 'knowledge_scope')}
        {field('背景故事', 'background', true)}
        {field('外观', 'appearance', true)}
        {field('禁忌话题（逗号或空格分隔）', 'taboo_topics')}
      </div>
      <div className={styles.actions}>
        <button onClick={onCancel} disabled={saving}>取消</button>
        <button onClick={handleSave} disabled={saving} className={styles.saveBtn}>
          {saving ? '保存中...' : '保存'}
        </button>
      </div>
    </div>
  );
}
