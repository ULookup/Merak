import { useState, useEffect, useCallback } from 'react';
import { api } from '../api/client';
import type { StoryChapter, StoryScene } from '../api/types';
import { useAppState } from '../AppState';
import styles from './ChapterEditor.module.css';

interface Props {
  chapterId: string;
  worldId: string;
}

interface ContextEntity {
  kind: 'agent' | 'location' | 'foreshadow' | 'secret';
  id: string;
  label: string;
  detail?: string;
}

export default function ChapterEditor({ chapterId, worldId }: Props) {
  const { state } = useAppState();
  const chapter = state.storyOverview?.current_chapter;
  const scene = state.storyOverview?.current_scene;

  const [title, setTitle] = useState(chapter?.title ?? '');
  const [content, setContent] = useState('');
  const [saveStatus, setSaveStatus] = useState<'idle' | 'saving' | 'saved'>('idle');
  const [showContext, setShowContext] = useState(false);
  const [entities, setEntities] = useState<ContextEntity[]>([]);

  // Build entity list from current world data
  useEffect(() => {
    const items: ContextEntity[] = [];
    state.agents.forEach(a => {
      items.push({ kind: 'agent', id: a.id, label: a.display_name || a.name, detail: a.kind });
    });
    state.foreshadowing.forEach(f => {
      items.push({ kind: 'foreshadow', id: f.id, label: f.content.slice(0, 40), detail: f.status });
    });
    state.secrets.forEach(s => {
      items.push({ kind: 'secret', id: s.id, label: s.title ?? s.truth?.slice(0, 40) ?? s.id, detail: s.status });
    });
    setEntities(items);
  }, [state.agents, state.foreshadowing, state.secrets]);

  const wordCount = content.trim() ? content.trim().split(/\s+/).length : 0;
  const charCount = content.length;

  const insertTag = useCallback((entity: ContextEntity) => {
    const tag = entity.kind === 'agent'
      ? `@agent{name=${entity.label}}`
      : entity.kind === 'foreshadow'
        ? `@foreshadow{id=${entity.id}}`
        : entity.kind === 'secret'
          ? `@secret{id=${entity.id}}`
          : `@location{name=${entity.label}}`;
    setContent(prev => prev + (prev ? '\n' : '') + tag);
  }, []);

  const handleSave = async () => {
    setSaveStatus('saving');
    try {
      await api.patchChapter(worldId, chapterId, {
        title,
        content,
      });
      setSaveStatus('saved');
      setTimeout(() => setSaveStatus('idle'), 2000);
    } catch {
      setSaveStatus('idle');
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 's') {
      e.preventDefault();
      handleSave();
    }
  };

  return (
    <div className={styles.container}>
      <div className={styles.toolbar}>
        <input
          className={styles.titleInput}
          value={title}
          onChange={e => setTitle(e.target.value)}
          placeholder="章节标题"
        />
        <div className={styles.toolbarRight}>
          <span className={styles.stats}>
            {wordCount} 词 | {charCount} 字
          </span>
          <button
            className={styles.contextBtn}
            onClick={() => setShowContext(!showContext)}
          >
            {showContext ? '隐藏上下文' : '上下文'}
          </button>
          <button className={styles.reviewBtn}>审阅</button>
          <button
            className={styles.saveBtn}
            onClick={handleSave}
            disabled={saveStatus === 'saving'}
          >
            {saveStatus === 'saving' ? '保存中...' : saveStatus === 'saved' ? '已保存' : '保存'}
          </button>
        </div>
      </div>

      <div className={styles.editorBody}>
        <textarea
          className={styles.textarea}
          value={content}
          onChange={e => setContent(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder="在此写作...&#10;&#10;使用 @agent{name=角色名} 引用角色&#10;使用 @foreshadow{id=伏笔ID} 引用伏笔&#10;使用 @secret{id=秘密ID} 引用秘密"
          spellCheck={false}
        />

        {showContext && (
          <aside className={styles.contextPanel}>
            <div className={styles.contextTitle}>创作上下文</div>
            {chapter && (
              <section className={styles.ctxSection}>
                <div className={styles.ctxSectionTitle}>当前章节</div>
                <div>第{chapter.number}章：{chapter.title}</div>
                <div className={styles.ctxMeta}>状态：{chapter.status} | {chapter.scene_count} 个场景</div>
              </section>
            )}
            {scene && (
              <section className={styles.ctxSection}>
                <div className={styles.ctxSectionTitle}>当前场景</div>
                <div>{scene.title}</div>
                <div className={styles.ctxMeta}>时间：{scene.world_time}</div>
                {scene.participant_ids.length > 0 && (
                  <div className={styles.ctxMeta}>
                    参与者：{scene.participant_ids.map(pid => {
                      const agent = state.agents.find(a => a.id === pid);
                      return agent?.display_name || agent?.name || pid;
                    }).join(', ')}
                  </div>
                )}
              </section>
            )}
            <section className={styles.ctxSection}>
              <div className={styles.ctxSectionTitle}>实体标签</div>
              <div className={styles.entityList}>
                {entities.map(e => (
                  <button
                    key={`${e.kind}-${e.id}`}
                    className={`${styles.entityTag} ${styles[e.kind]}`}
                    onClick={() => insertTag(e)}
                    title={e.detail}
                  >
                    {e.kind === 'agent' ? '@' : e.kind === 'foreshadow' ? '◈' : '⚷'} {e.label}
                  </button>
                ))}
              </div>
            </section>
          </aside>
        )}
      </div>
    </div>
  );
}
