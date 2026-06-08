import { useState } from 'react';
import { useAppState } from '../../AppState';
import { api } from '../../api/client';
import styles from './SkillBrowser.module.css';

interface SkillInfo {
  name: string;
  description: string;
  version: string;
}

// Built-in skills — populated from config/skills/ at build time
const BUILT_IN_SKILLS: SkillInfo[] = [
  {
    name: 'create_character_card',
    description: '创建完整的角色卡片，包含性格、背景、声音指纹',
    version: '1.0.0',
  },
  {
    name: 'write_scene',
    description: '根据场景大纲写作具体场景内容',
    version: '1.0.0',
  },
  {
    name: 'review_chapter',
    description: '回顾章节，检查一致性、节奏和伏笔',
    version: '1.0.0',
  },
  {
    name: 'plant_foreshadow',
    description: '在场景中布置伏笔，与后续情节关联',
    version: '1.0.0',
  },
  {
    name: 'end_scene_cleanup',
    description: '场景结束后的清理工作：更新日记、关系、伏笔状态',
    version: '1.0.0',
  },
];

export default function SkillBrowser() {
  const { state } = useAppState();
  const [expanded, setExpanded] = useState<string | null>(null);
  const [sending, setSending] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const handleInvoke = async (skillName: string) => {
    if (!state.sessionId || !state.worldId) return;
    setSending(skillName);
    try {
      await api.startRun(state.sessionId, `/skill ${skillName}`, state.selectedModel);
    } catch (e: unknown) {
      setError((e as Error).message || '调用失败');
      setTimeout(() => setError(null), 5000);
    } finally {
      setSending(null);
    }
  };

  const toggle = (name: string) => {
    setExpanded(prev => prev === name ? null : name);
  };

  return (
    <div className={styles.container}>
      <div className={styles.title}>Skills</div>
      <div className={styles.list}>
        {BUILT_IN_SKILLS.map(skill => {
          const isOpen = expanded === skill.name;
          const isLoading = sending === skill.name;
          return (
            <div key={skill.name} className={`${styles.skill} ${isOpen ? styles.open : ''}`}>
              <div className={styles.skillHeader} onClick={() => toggle(skill.name)}>
                <span className={styles.skillName}>{skill.name}</span>
                <span className={styles.version}>v{skill.version}</span>
                <span className={styles.chevron}>{isOpen ? '▾' : '▸'}</span>
              </div>
              {isOpen && (
                <div className={styles.skillBody}>
                  <p className={styles.desc}>{skill.description}</p>
                  <button
                    className={styles.invokeBtn}
                    onClick={() => handleInvoke(skill.name)}
                    disabled={!!isLoading || !state.sessionId}
                  >
                    {isLoading ? '调用中...' : '调用技能'}
                  </button>
                </div>
              )}
            </div>
          );
        })}
      </div>
      {error && (
        <p className={styles.error}>{error}</p>
      )}
      {!state.sessionId && (
        <p className={styles.hint}>创建会话后可使用技能</p>
      )}
    </div>
  );
}
