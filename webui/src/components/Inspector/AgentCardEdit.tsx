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

const LIST_SPLIT_PATTERN = /[,，、\s]+/;

export default function AgentCardEdit({ worldId, agentId, detail, onSave, onCancel }: Props) {
  const cc = detail.character_card;
  const [fields, setFields] = useState<Record<string, string>>({
    age: String(cc.age ?? ''),
    gender: cc.gender ?? '',
    race: cc.race ?? '',
    identity: cc.identity ?? '',
    core_traits: cc.core_traits?.join(', ') ?? '',
    emotional_tendency: cc.emotional_tendency ?? '',
    speaking_style: cc.speaking_style ?? '',
    core_desire: cc.core_desire ?? '',
    deep_fear: cc.deep_fear ?? '',
    daily_goal: cc.daily_goal ?? '',
    background: cc.background ?? '',
    knowledge_scope: cc.knowledge_scope ?? '',
    appearance: cc.appearance ?? '',
    taboo_topics: cc.taboo_topics?.join(', ') ?? '',
  });
  const [saving, setSaving] = useState(false);
  const [conflict, setConflict] = useState(false);

  const displayName = detail.display_name || detail.name;
  const set = (key: string, value: string) => setFields((prev) => ({ ...prev, [key]: value }));

  const handleSave = async () => {
    setSaving(true);
    setConflict(false);
    try {
      const payload: Record<string, unknown> = {};
      for (const [key, value] of Object.entries(fields)) {
        if (key === 'core_traits' || key === 'taboo_topics') {
          const values = value.split(LIST_SPLIT_PATTERN).filter(Boolean);
          if (values.length > 0) payload[key] = values;
        } else if (value !== '' && value !== String(cc[key as keyof typeof cc] ?? '')) {
          payload[key] = key === 'age' ? Number(value) : value;
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
        <textarea value={fields[key] ?? ''} onChange={(event) => set(key, event.target.value)} rows={4} />
      ) : (
        <input value={fields[key] ?? ''} onChange={(event) => set(key, event.target.value)} />
      )}
    </label>
  );

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <span>Character Card</span>
        <h3>Edit {displayName}</h3>
      </div>
      {conflict && (
        <div className={styles.conflictBanner}>
          This card was updated elsewhere. Refresh the character profile before editing again.
        </div>
      )}
      <div className={styles.form}>
        {field('Age', 'age')}
        {field('Gender', 'gender')}
        {field('Race', 'race')}
        {field('Identity', 'identity')}
        {field('Core traits (comma or space separated)', 'core_traits')}
        {field('Emotional tendency', 'emotional_tendency')}
        {field('Speaking style', 'speaking_style')}
        {field('Core desire', 'core_desire')}
        {field('Deep fear', 'deep_fear')}
        {field('Daily goal', 'daily_goal')}
        {field('Knowledge scope', 'knowledge_scope')}
        {field('Background', 'background', true)}
        {field('Appearance', 'appearance', true)}
        {field('Taboo topics (comma or space separated)', 'taboo_topics')}
      </div>
      <div className={styles.actions}>
        <button onClick={onCancel} disabled={saving}>Cancel</button>
        <button onClick={handleSave} disabled={saving} className={styles.saveBtn}>
          {saving ? 'Saving...' : 'Save'}
        </button>
      </div>
    </div>
  );
}
