import { useState } from 'react';
import { api, formatApiError } from '../api/client';
import type { ExportResult } from '../api/types';
import styles from './ExportDialog.module.css';

interface Chapter {
  id: string;
  title: string;
  number: number;
}

interface Props {
  worldId: string;
  chapters: Chapter[];
  onClose: () => void;
}

export default function ExportDialog({ worldId, chapters, onClose }: Props) {
  const [selected, setSelected] = useState<Set<string>>(
    () => new Set(chapters.map((c) => c.id)),
  );
  const [title, setTitle] = useState('');
  const [author, setAuthor] = useState('');
  const [exporting, setExporting] = useState(false);
  const [result, setResult] = useState<ExportResult | null>(null);
  const [error, setError] = useState<string | null>(null);

  function toggleChapter(id: string) {
    setSelected((prev) => {
      const next = new Set(prev);
      if (next.has(id)) {
        next.delete(id);
      } else {
        next.add(id);
      }
      return next;
    });
    setResult(null);
    setError(null);
  }

  function handleSelectAll() {
    setSelected(new Set(chapters.map((c) => c.id)));
    setResult(null);
    setError(null);
  }

  function handleDeselectAll() {
    setSelected(new Set());
    setResult(null);
    setError(null);
  }

  async function handleExport() {
    if (selected.size === 0 || !title.trim()) return;

    setExporting(true);
    setError(null);
    setResult(null);

    try {
      const res = await api.exportChapters(worldId, {
        chapter_ids: [...selected],
        title: title.trim(),
        author: author.trim() || undefined,
      });
      setResult(res);
    } catch (e: unknown) {
      setError(formatApiError(e, '导出失败，请稍后重试。'));
    } finally {
      setExporting(false);
    }
  }

  const canExport = selected.size > 0 && title.trim().length > 0 && !exporting;

  return (
    <div className={styles.overlay} role="dialog" aria-label="导出 TXT">
      <div className={styles.card}>
        <h2 className={styles.title}>导出 TXT</h2>

        <div className={styles.field}>
          <label className={styles.label}>选择章节</label>
          <div className={styles.chapterList}>
            {chapters.map((ch) => (
              <label key={ch.id} className={styles.chapterItem}>
                <input
                  type="checkbox"
                  checked={selected.has(ch.id)}
                  onChange={() => toggleChapter(ch.id)}
                  disabled={exporting}
                />
                <span>
                  第{ch.number}章 {ch.title}
                </span>
              </label>
            ))}
          </div>
          <div style={{ display: 'flex', gap: 8, marginTop: 6 }}>
            <button
              type="button"
              className={styles.btnSecondary}
              onClick={handleSelectAll}
              disabled={exporting}
              style={{ padding: '4px 10px', fontSize: '0.8rem' }}
            >
              全选
            </button>
            <button
              type="button"
              className={styles.btnSecondary}
              onClick={handleDeselectAll}
              disabled={exporting}
              style={{ padding: '4px 10px', fontSize: '0.8rem' }}
            >
              取消全选
            </button>
          </div>
        </div>

        <div className={styles.field}>
          <label className={styles.label}>书名</label>
          <input
            className={styles.input}
            type="text"
            value={title}
            onChange={(e) => setTitle(e.target.value)}
            placeholder="请输入书名"
            disabled={exporting}
          />
        </div>

        <div className={styles.field}>
          <label className={styles.label}>作者名（选填）</label>
          <input
            className={styles.input}
            type="text"
            value={author}
            onChange={(e) => setAuthor(e.target.value)}
            placeholder="请输入作者名"
            disabled={exporting}
          />
        </div>

        {result && (
          <div className={styles.result}>
            <div>导出成功！</div>
            <div>文件路径：{result.file_path}</div>
            <div>总字符数：{result.total_chars.toLocaleString()}</div>
          </div>
        )}

        {error && <div className={styles.error}>{error}</div>}

        <div className={styles.actions}>
          <button
            className={styles.btnSecondary}
            onClick={onClose}
            disabled={exporting}
          >
            取消
          </button>
          <button
            className={styles.btnPrimary}
            onClick={handleExport}
            disabled={!canExport}
          >
            {exporting ? '导出中...' : '导出'}
          </button>
        </div>
      </div>
    </div>
  );
}
