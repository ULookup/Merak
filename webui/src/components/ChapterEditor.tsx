import { useCallback, useEffect, useState, type KeyboardEvent } from 'react';
import { api } from '../api/client';
import { useAppState } from '../AppState';
import styles from './ChapterEditor.module.css';

function chapterFilePath(worldId: string, chapterId: string) {
  return `chapters/${worldId}/${chapterId}.md`;
}

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
  const [contentLoaded, setContentLoaded] = useState(false);
  const [saveStatus, setSaveStatus] = useState<'idle' | 'saving' | 'saved'>('idle');
  const [showContext, setShowContext] = useState(false);
  const [entities, setEntities] = useState<ContextEntity[]>([]);

  useEffect(() => {
    const items: ContextEntity[] = [];
    state.agents.forEach((agent) => {
      items.push({
        kind: 'agent',
        id: agent.id,
        label: agent.display_name || agent.name,
        detail: agent.kind,
      });
    });
    state.foreshadowing.forEach((item) => {
      items.push({
        kind: 'foreshadow',
        id: item.id,
        label: item.content.slice(0, 40),
        detail: item.status,
      });
    });
    state.secrets.forEach((secret) => {
      items.push({
        kind: 'secret',
        id: secret.id,
        label: secret.title ?? secret.truth?.slice(0, 40) ?? secret.id,
        detail: secret.status,
      });
    });
    setEntities(items);
  }, [state.agents, state.foreshadowing, state.secrets]);

  useEffect(() => {
    setTitle(chapter?.title ?? '');
  }, [chapter?.title]);

  useEffect(() => {
    if (!chapterId || !worldId) return;
    let cancelled = false;
    const path = chapterFilePath(worldId, chapterId);
    setContentLoaded(false);
    api
      .readWorkspaceFile(path)
      .then((data) => {
        if (!cancelled) {
          setContent(data.file.content);
          setContentLoaded(true);
        }
      })
      .catch(() => {
        if (!cancelled) {
          setContent('');
          setContentLoaded(true);
        }
      });
    return () => {
      cancelled = true;
    };
  }, [chapterId, worldId]);

  const trimmed = content.trim();
  const cjkChars = (trimmed.match(/[一-鿿]/g) || []).length;
  const isCJK = trimmed.length > 0 && cjkChars / trimmed.length > 0.3;
  const wordCount = trimmed ? (isCJK ? cjkChars : trimmed.split(/\s+/).length) : 0;
  const charCount = content.length;

  const insertTag = useCallback((entity: ContextEntity) => {
    const tag =
      entity.kind === 'agent'
        ? `@agent{name=${entity.label}}`
        : entity.kind === 'foreshadow'
          ? `@foreshadow{id=${entity.id}}`
          : entity.kind === 'secret'
            ? `@secret{id=${entity.id}}`
            : `@location{name=${entity.label}}`;
    setContent((previous) => previous + (previous ? '\n' : '') + tag);
  }, []);

  const handleSave = async () => {
    setSaveStatus('saving');
    try {
      const path = chapterFilePath(worldId, chapterId);
      await api.saveWorkspaceFile(path, content);
      if (title !== (chapter?.title ?? '')) {
        await api.patchChapter(worldId, chapterId, { title }).catch(() => {});
      }
      setSaveStatus('saved');
      setTimeout(() => setSaveStatus('idle'), 2000);
    } catch {
      setSaveStatus('idle');
    }
  };

  const handleKeyDown = (event: KeyboardEvent<HTMLTextAreaElement>) => {
    if ((event.ctrlKey || event.metaKey) && event.key === 's') {
      event.preventDefault();
      handleSave();
    }
  };

  return (
    <div className={styles.container}>
      <div className={styles.toolbar}>
        <input
          className={styles.titleInput}
          value={title}
          onChange={(event) => setTitle(event.target.value)}
          placeholder="Chapter title"
        />
        <div className={styles.toolbarRight}>
          <span className={styles.stats}>
            {isCJK ? `${wordCount} 字` : `${wordCount} words`} &middot; {charCount} chars
          </span>
          <button
            className={styles.contextBtn}
            onClick={() => setShowContext(!showContext)}
            type="button"
          >
            {showContext ? 'Hide context' : 'Context'}
          </button>
          <button className={styles.reviewBtn} type="button">
            Review
          </button>
          <button
            className={styles.saveBtn}
            onClick={handleSave}
            disabled={saveStatus === 'saving' || !contentLoaded}
            type="button"
          >
            {saveStatus === 'saving' ? 'Saving...' : saveStatus === 'saved' ? 'Saved' : 'Save'}
          </button>
        </div>
      </div>

      <div className={styles.editorBody}>
        <textarea
          className={styles.textarea}
          value={content}
          onChange={(event) => setContent(event.target.value)}
          onKeyDown={handleKeyDown}
          placeholder={[
            'Write here...',
            '',
            'Use @agent{name=character_name} to reference a character',
            'Use @foreshadow{id=thread_id} to reference a thread',
            'Use @secret{id=secret_id} to reference a secret',
          ].join('\n')}
          spellCheck={false}
        />

        {showContext && (
          <aside className={styles.contextPanel}>
            <div className={styles.contextTitle}>Writing Context</div>
            {chapter && (
              <section className={styles.ctxSection}>
                <div className={styles.ctxSectionTitle}>Current Chapter</div>
                <div>
                  Chapter {chapter.number}: {chapter.title}
                </div>
                <div className={styles.ctxMeta}>
                  Status: {chapter.status} &middot; {chapter.scene_count} scenes
                </div>
              </section>
            )}
            {scene && (
              <section className={styles.ctxSection}>
                <div className={styles.ctxSectionTitle}>Current Scene</div>
                <div>{scene.title}</div>
                <div className={styles.ctxMeta}>Time: {scene.world_time}</div>
                {scene.participant_ids.length > 0 && (
                  <div className={styles.ctxMeta}>
                    Participants:{' '}
                    {scene.participant_ids
                      .map((participantId) => {
                        const agent = state.agents.find((item) => item.id === participantId);
                        return agent?.display_name || agent?.name || participantId;
                      })
                      .join(', ')}
                  </div>
                )}
              </section>
            )}
            <section className={styles.ctxSection}>
              <div className={styles.ctxSectionTitle}>Entity Tags</div>
              <div className={styles.entityList}>
                {entities.map((entity) => (
                  <button
                    key={`${entity.kind}-${entity.id}`}
                    className={`${styles.entityTag} ${styles[entity.kind]}`}
                    onClick={() => insertTag(entity)}
                    title={entity.detail}
                    type="button"
                  >
                    {entity.kind === 'agent'
                      ? '@'
                      : entity.kind === 'foreshadow'
                        ? '#'
                        : 'key'}{' '}
                    {entity.label}
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
