import { useRef, useState } from 'react';
import { ImagePlus } from 'lucide-react';
import { api, formatApiError } from '../../api/client';
import type { AgentImage, AgentImageType } from '../../api/types';
import styles from './AgentImageGallery.module.css';

const SIMPLE_UPLOAD_LIMIT = 10 * 1024 * 1024;
const CHUNK_SIZE = 2 * 1024 * 1024;

function formatBytes(value: number) {
  if (value < 1024) return `${value} B`;
  if (value < 1024 * 1024) return `${Math.round(value / 102.4) / 10} KB`;
  return `${Math.round(value / 1024 / 102.4) / 10} MB`;
}

interface Props {
  worldId: string;
  agentId: string;
  imageType: AgentImageType;
  images: AgentImage[];
  onChanged: () => Promise<void> | void;
}

export default function AgentImageGallery({
  worldId,
  agentId,
  imageType,
  images,
  onChanged,
}: Props) {
  const inputRef = useRef<HTMLInputElement>(null);
  const cancelRef = useRef(false);
  const [busy, setBusy] = useState(false);
  const [progress, setProgress] = useState<number | null>(null);
  const [error, setError] = useState<string | null>(null);

  const title = imageType === 'avatar' ? 'Avatar' : 'Character Designs';
  const hint =
    imageType === 'avatar'
      ? 'One primary portrait is used across agent lists and cards.'
      : 'Reference sheets and design images stay visible in the character inspector.';

  async function uploadChunked(file: File) {
    const init = await api.initAgentImageUpload(worldId, agentId, {
      image_type: imageType,
      file_name: file.name,
      mime_type: file.type || 'application/octet-stream',
      total_size: file.size,
      chunk_size: CHUNK_SIZE,
    });
    for (let index = 0; index < init.chunks_total; index++) {
      if (cancelRef.current) {
        await api.cancelAgentImageUpload(worldId, agentId, init.upload_id).catch(() => undefined);
        throw new Error('Upload cancelled');
      }
      const start = index * init.chunk_size;
      const chunk = file.slice(start, Math.min(start + init.chunk_size, file.size));
      await api.uploadAgentImageChunk(worldId, agentId, init.upload_id, index, chunk);
      setProgress(Math.round(((index + 1) / init.chunks_total) * 100));
    }
    await api.completeAgentImageUpload(worldId, agentId, init.upload_id);
  }

  async function handleFiles(files: FileList | null) {
    const file = files?.[0];
    if (!file) return;
    cancelRef.current = false;
    setBusy(true);
    setError(null);
    setProgress(file.size > SIMPLE_UPLOAD_LIMIT ? 0 : null);
    try {
      if (file.size > SIMPLE_UPLOAD_LIMIT) {
        await uploadChunked(file);
      } else {
        await api.uploadAgentImage(worldId, agentId, imageType, file);
      }
      await onChanged();
    } catch (e) {
      setError(formatApiError(e, '图片上传失败。'));
    } finally {
      setBusy(false);
      setProgress(null);
      if (inputRef.current) inputRef.current.value = '';
    }
  }

  async function mutateImage(task: () => Promise<unknown>) {
    setBusy(true);
    setError(null);
    try {
      await task();
      await onChanged();
    } catch (e) {
      setError(formatApiError(e));
    } finally {
      setBusy(false);
    }
  }

  return (
    <section className={styles.gallery}>
      <div className={styles.toolbar}>
        <div className={styles.toolbarText}>
          <strong>{title}</strong>
          <span>{hint}</span>
        </div>
        <label className={`${styles.uploadBtn} ${busy ? styles.uploadBtnDisabled : ''}`}>
          <ImagePlus size={14} aria-hidden="true" />
          Upload
          <input
            ref={inputRef}
            type="file"
            accept="image/png,image/jpeg,image/webp,image/gif"
            disabled={busy}
            onChange={(event) => void handleFiles(event.currentTarget.files)}
          />
        </label>
      </div>

      {progress !== null && (
        <div className={styles.progress}>
          <div className={styles.progressHeader}>
            <span>Uploading {progress}%</span>
            <button className={styles.cancelBtn} onClick={() => (cancelRef.current = true)}>
              Cancel
            </button>
          </div>
          <div className={styles.progressBar}>
            <div className={styles.progressFill} style={{ width: `${progress}%` }} />
          </div>
        </div>
      )}

      {error && <div className={styles.error}>{error}</div>}

      {images.length === 0 ? (
        <div className={styles.empty}>
          No {imageType === 'avatar' ? 'avatar' : 'design images'} yet.
        </div>
      ) : (
        <div className={styles.grid}>
          {images.map((image) => (
            <article className={styles.imageCard} key={image.id}>
              <div className={styles.imageFrame}>
                <img src={api.imageUrl(image.url)} alt={image.original_name} />
                {image.is_primary && <span className={styles.primary}>Primary</span>}
              </div>
              <div className={styles.meta}>
                <strong title={image.original_name}>{image.original_name}</strong>
                <span>{formatBytes(image.file_size_bytes)}</span>
              </div>
              <div className={styles.actions}>
                <button
                  className={styles.actionBtn}
                  disabled={busy || image.is_primary}
                  onClick={() =>
                    void mutateImage(() =>
                      api.updateAgentImage(worldId, agentId, image.id, { is_primary: true }),
                    )
                  }
                >
                  Primary
                </button>
                <button
                  className={styles.dangerBtn}
                  disabled={busy}
                  onClick={() =>
                    void mutateImage(() => api.deleteAgentImage(worldId, agentId, image.id))
                  }
                >
                  Delete
                </button>
              </div>
            </article>
          ))}
        </div>
      )}
    </section>
  );
}
