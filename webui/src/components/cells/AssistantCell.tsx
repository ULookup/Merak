import { useState } from 'react';
import ReactMarkdown from 'react-markdown';
import { Check, Copy } from 'lucide-react';
import rehypeHighlight from 'rehype-highlight';
import styles from './Cells.module.css';

interface Props {
  text: string;
}

export default function AssistantCell({ text }: Props) {
  const [copied, setCopied] = useState(false);

  async function copy() {
    try {
      await navigator.clipboard.writeText(text);
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    } catch {
      /* clipboard not available */
    }
  }

  return (
    <div style={{ position: 'relative' }}>
      <div className={`${styles.msg} ${styles.msgAssistant}`}>
        <ReactMarkdown rehypePlugins={[rehypeHighlight]}>{text}</ReactMarkdown>
      </div>
      <button
        onClick={copy}
        data-testid="copy-msg-btn"
        className={`${styles.copyBtn} ${copied ? styles.copyBtnDone : ''}`}
        aria-label={copied ? 'Copied' : 'Copy message'}
      >
        {copied ? (
          <>
            <Check size={13} aria-hidden="true" strokeWidth={2.6} />
            Copied
          </>
        ) : (
          <>
            <Copy size={13} aria-hidden="true" strokeWidth={2.3} />
            Copy
          </>
        )}
      </button>
    </div>
  );
}
