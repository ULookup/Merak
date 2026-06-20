import { useState } from 'react';
import { ArrowDown, ArrowUp, BookOpen, Edit3, RefreshCw } from 'lucide-react';
import { api } from '../api/client';
import type { StoryChapter } from '../api/types';
import { worldbuildingApi } from '../api/worldbuilding';
import ChapterEditor from '../components/ChapterEditor';
import PageState from '../components/layout/PageState';
import { useResource } from '../hooks/useResource';
import styles from './ChaptersPage.module.css';

export default function ChaptersPage({ worldId }: { worldId: string }) {
  const resource = useResource(`chapters:${worldId}`, () => api.listChapters(worldId));
  const [localOrder, setLocalOrder] = useState<{ worldId: string; items: StoryChapter[] } | null>(
    null,
  );
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [reorderError, setReorderError] = useState<string | null>(null);
  const chapters =
    localOrder?.worldId === worldId ? localOrder.items : (resource.data?.chapters ?? []);
  const selected = chapters.find((chapter) => chapter.id === selectedId) ?? null;

  async function moveChapter(index: number, offset: -1 | 1) {
    const target = index + offset;
    if (target < 0 || target >= chapters.length) return;
    const previous = chapters;
    const next = [...chapters];
    [next[index], next[target]] = [next[target], next[index]];
    setLocalOrder({ worldId, items: next });
    setReorderError(null);
    try {
      await worldbuildingApi.reorderChapters(
        worldId,
        next.map((chapter) => chapter.id),
      );
    } catch (error) {
      setLocalOrder({ worldId, items: previous });
      setReorderError(error instanceof Error ? error.message : 'Unable to reorder chapters.');
    }
  }

  if (!resource.data) {
    return (
      <PageState
        loading={resource.status === 'loading'}
        loadingLabel="Loading chapters"
        error={resource.error}
        onRetry={resource.retry}
      />
    );
  }

  if (!chapters.length) {
    return (
      <main className={styles.page}>
        <PageState
          isEmpty
          emptyTitle="No chapters yet"
          emptyDescription="Chapters will appear here after they are created for this world."
        />
      </main>
    );
  }

  return (
    <main className={styles.page}>
      <header className={styles.header}>
        <div>
          <span className={styles.eyebrow}>Narrative plan</span>
          <h1>Chapters</h1>
          <p>Arrange the manuscript and open a chapter without leaving the planning workspace.</p>
        </div>
        <button type="button" className={styles.refresh} onClick={resource.retry}>
          <RefreshCw aria-hidden="true" /> Refresh
        </button>
      </header>

      {resource.error ? (
        <div role="alert" className={styles.warning}>
          {resource.error.message}
        </div>
      ) : null}
      {reorderError ? (
        <div role="alert" className={styles.warning}>
          {reorderError}
        </div>
      ) : null}

      <section className={styles.summary} aria-label="Chapter summary">
        <div>
          <BookOpen aria-hidden="true" />
          <strong>{chapters.length}</strong>
          <span>Total chapters</span>
        </div>
        <div>
          <strong>{chapters.filter((chapter) => chapter.status === 'completed').length}</strong>
          <span>Completed</span>
        </div>
        <div>
          <strong>{chapters.reduce((sum, chapter) => sum + chapter.scene_count, 0)}</strong>
          <span>Scenes</span>
        </div>
      </section>

      <section className={styles.grid} aria-label="Chapter order">
        {chapters.map((chapter, index) => (
          <article
            className={selectedId === chapter.id ? styles.selectedCard : styles.card}
            key={chapter.id}
          >
            <div className={styles.cardTop}>
              <span>Chapter {chapter.number}</span>
              <span className={styles.status}>{chapter.status}</span>
            </div>
            <h2>{chapter.title}</h2>
            <p>
              {chapter.scene_count} {chapter.scene_count === 1 ? 'scene' : 'scenes'}
            </p>
            <div className={styles.actions}>
              <button
                type="button"
                onClick={() => setSelectedId(chapter.id)}
                aria-label={`Edit ${chapter.title}`}
              >
                <Edit3 aria-hidden="true" /> Edit
              </button>
              <button
                type="button"
                onClick={() => moveChapter(index, -1)}
                disabled={index === 0}
                aria-label={`Move ${chapter.title} previous`}
              >
                <ArrowUp aria-hidden="true" />
              </button>
              <button
                type="button"
                onClick={() => moveChapter(index, 1)}
                disabled={index === chapters.length - 1}
                aria-label={`Move ${chapter.title} next`}
              >
                <ArrowDown aria-hidden="true" />
              </button>
            </div>
          </article>
        ))}
      </section>

      {selected ? (
        <section className={styles.editor} aria-label={`Editing ${selected.title}`}>
          <ChapterEditor chapterId={selected.id} worldId={worldId} chapter={selected} />
        </section>
      ) : null}
    </main>
  );
}
