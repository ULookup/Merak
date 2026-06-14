import { useEffect, useState } from 'react';
import { api, formatApiError } from '../api/client';
import type { ChapterReviewItem } from '../api/types';
import styles from './ChapterReviewBanner.module.css';

interface Props {
  worldId: string;
  chapterId: string;
  chapterTitle?: string;
  onNewChapter: () => void;
  onRevise: () => void;
  onExport: () => void;
  onClose: () => void;
}

export default function ChapterReviewBanner({
  worldId,
  chapterId,
  chapterTitle,
  onNewChapter,
  onRevise,
  onExport,
  onClose,
}: Props) {
  const [review, setReview] = useState<ChapterReviewItem | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    async function fetchReview() {
      setLoading(true);
      setError(null);

      try {
        const result = await api.getChapterReview(worldId, chapterId);
        if (!cancelled) {
          setReview(result.review);
        }
      } catch (e: unknown) {
        if (!cancelled) {
          setError(formatApiError(e, '获取章节回顾失败。'));
        }
      } finally {
        if (!cancelled) {
          setLoading(false);
        }
      }
    }

    fetchReview();

    return () => {
      cancelled = true;
    };
  }, [worldId, chapterId]);

  if (loading) {
    return (
      <div className={styles.banner}>
        <div className={styles.loading}>正在获取章节回顾...</div>
      </div>
    );
  }

  if (error || !review) {
    return (
      <div className={styles.banner}>
        <div className={styles.error}>{error || '章节数据不可用。'}</div>
      </div>
    );
  }

  const displayTitle = chapterTitle || review.title;

  return (
    <div className={styles.banner}>
      <div className={styles.header}>
        <div>
          <span className={styles.title}>章节回顾</span>
          {displayTitle && <span className={styles.subtitle}>{displayTitle}</span>}
        </div>
        <button className={styles.closeBtn} onClick={onClose} aria-label="关闭">
          ✕
        </button>
      </div>

      <div className={styles.stats}>
        <span className={styles.stat}>
          字数：<span className={styles.statValue}>{review.word_count}</span>
        </span>
        <span className={styles.stat}>
          出场角色：<span className={styles.statValue}>{review.character_names.length}</span>
        </span>
        <span className={styles.stat}>
          新伏笔：<span className={styles.statValue}>{review.foreshadowing_planted.length}</span>
        </span>
        <span className={styles.stat}>
          回收伏笔：<span className={styles.statValue}>{review.foreshadowing_paid.length}</span>
        </span>
      </div>

      {review.writing_advice && (
        <div className={styles.advice}>{review.writing_advice}</div>
      )}

      <div className={styles.actions}>
        <button className={styles.btnPrimary} onClick={onNewChapter}>
          开始下一章
        </button>
        <button className={styles.btnSecondary} onClick={onRevise}>
          修改本章
        </button>
        <button className={styles.btnSecondary} onClick={onExport}>
          导出
        </button>
      </div>
    </div>
  );
}
