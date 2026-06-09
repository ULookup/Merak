import { Brain, Copy, Loader2, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import styles from './AgentPromptViewer.module.css';

interface Props {
  agentId: string;
  agentName: string;
  onClose: () => void;
}

export default function AgentPromptViewer({ agentId, agentName, onClose }: Props) {
  const [prompt, setPrompt] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [copied, setCopied] = useState(false);

  useEffect(() => {
    function onKey(e: KeyboardEvent) { if (e.key === 'Escape') onClose(); }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);

  useEffect(() => {
    api.getAgentPrompt(agentId)
      .then(res => { setPrompt(res.prompt); setLoading(false); })
      .catch(e => { setError((e as Error).message); setLoading(false); });
  }, [agentId]);

  async function handleCopy() {
    if (!prompt) return;
    try {
      await navigator.clipboard.writeText(prompt);
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    } catch {
      // fallback: select text manually
    }
  }

  return (
    <div className={styles.scrim} role="presentation">
      <section className={styles.modal} role="dialog" aria-modal="true" aria-label="Agent prompt">
        <button className={styles.closeBtn} onClick={onClose} aria-label="Close">
          <X size={17} aria-hidden="true" strokeWidth={2.4} />
        </button>
        <div className={styles.header}>
          <div className={styles.iconWrap}>
            <Brain size={28} aria-hidden="true" strokeWidth={2.1} />
          </div>
          <div>
            <div className={styles.kicker}>System Prompt</div>
            <h2>{agentName}</h2>
          </div>
        </div>

        {loading && (
          <div className={styles.loading}>
            <Loader2 size={20} aria-hidden="true" className={styles.spin} />
            <span>Loading prompt...</span>
          </div>
        )}

        {error && <div className={styles.error}>{error}</div>}

        {prompt && (
          <>
            <div className={styles.toolbar}>
              <span className={styles.charCount}>{prompt.length} characters</span>
              <button className={styles.copyBtn} onClick={handleCopy}>
                <Copy size={14} aria-hidden="true" />
                {copied ? 'Copied' : 'Copy'}
              </button>
            </div>
            <pre className={styles.promptBlock}>{prompt}</pre>
          </>
        )}

        <div className={styles.actions}>
          <button className={styles.secondary} onClick={onClose}>Close</button>
        </div>
      </section>
    </div>
  );
}
