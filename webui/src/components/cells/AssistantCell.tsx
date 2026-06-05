import { useState } from 'react';
import ReactMarkdown from 'react-markdown';
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
    } catch { /* clipboard not available */ }
  }

  return (
    <div style={{ position: 'relative' }}>
      <div className={`${styles.msg} ${styles.msgAssistant}`}>
        <ReactMarkdown rehypePlugins={[rehypeHighlight]}>{text}</ReactMarkdown>
      </div>
      <button
        onClick={copy}
        data-testid="copy-msg-btn"
        style={{
          position: 'absolute',
          top: 6,
          right: 8,
          background: copied ? '#14532d' : '#1f2230',
          border: copied ? '1px solid #22c55e' : '1px solid #2a2c33',
          color: copied ? '#22c55e' : '#555768',
          borderRadius: 4,
          padding: '2px 8px',
          fontSize: 11,
          cursor: 'pointer',
        }}
        aria-label={copied ? 'Copied' : 'Copy message'}
      >
        {copied ? 'Copied' : 'Copy'}
      </button>
    </div>
  );
}
