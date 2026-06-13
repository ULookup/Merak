import {
  Children,
  isValidElement,
  useEffect,
  useState,
  type ComponentPropsWithoutRef,
} from 'react';
import ReactMarkdown from 'react-markdown';
import { Check, Copy } from 'lucide-react';
import rehypeHighlight from 'rehype-highlight';
import remarkGfm from 'remark-gfm';
import type { StatusLabel } from '../../api/types';
import styles from './Cells.module.css';

interface Props {
  text: string;
  status?: StatusLabel;
  pending?: boolean;
  thinkingStartedAt?: number;
  thinkingCompletedAt?: number;
}

function formatElapsed(ms: number) {
  if (ms < 1000) return '<1s';
  const seconds = Math.round(ms / 100) / 10;
  if (seconds < 60) return `${seconds.toFixed(seconds % 1 === 0 ? 0 : 1)}s`;
  const minutes = Math.floor(seconds / 60);
  const rest = Math.round(seconds % 60);
  return `${minutes}m ${rest}s`;
}

function labelForStatus(status?: StatusLabel) {
  if (status === 'acting') return 'Using tools';
  if (status === 'observing') return 'Reading results';
  if (status === 'responding') return 'Writing';
  if (status === 'waiting_approval') return 'Waiting for approval';
  return 'Thinking';
}

function textFromChildren(children: ComponentPropsWithoutRef<'code'>['children']): string {
  return Children.toArray(children)
    .map((child) => {
      if (typeof child === 'string' || typeof child === 'number') return String(child);
      if (isValidElement<{ children?: ComponentPropsWithoutRef<'code'>['children'] }>(child)) {
        return textFromChildren(child.props.children);
      }
      return '';
    })
    .join('');
}

export default function AssistantCell({
  text,
  status,
  pending,
  thinkingStartedAt,
  thinkingCompletedAt,
}: Props) {
  const [copied, setCopied] = useState(false);
  const [now, setNow] = useState(() => Date.now());
  const hasText = text.trim().length > 0;
  const elapsed =
    thinkingStartedAt !== undefined ? (thinkingCompletedAt ?? now) - thinkingStartedAt : undefined;

  useEffect(() => {
    if (!pending || !thinkingStartedAt || thinkingCompletedAt) return undefined;
    const timer = window.setInterval(() => setNow(Date.now()), 250);
    return () => window.clearInterval(timer);
  }, [pending, thinkingCompletedAt, thinkingStartedAt]);

  async function copy() {
    if (!hasText) return;
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
        {elapsed !== undefined && (
          <div className={styles.thinkingMeta}>
            <span className={pending && !hasText ? styles.thinkingPulse : styles.thinkingDot} />
            <span>
              {pending && !hasText ? labelForStatus(status) : 'Thought'} for{' '}
              {formatElapsed(Math.max(0, elapsed))}
            </span>
          </div>
        )}
        {hasText ? (
          <ReactMarkdown
            remarkPlugins={[remarkGfm]}
            rehypePlugins={[rehypeHighlight]}
            components={{
              a({ children, ...props }) {
                return (
                  <a {...props} target="_blank" rel="noreferrer">
                    {children}
                  </a>
                );
              },
              code({ children, className, ...props }) {
                const match = /language-([\w-]+)/.exec(className ?? '');
                const language = match?.[1];
                const codeText = textFromChildren(children);
                const isBlock = codeText.includes('\n') || Boolean(language);

                if (!isBlock) {
                  return (
                    <code className={styles.inlineCode} {...props}>
                      {children}
                    </code>
                  );
                }

                return (
                  <code className={className} data-language={language ?? 'text'} {...props}>
                    {children}
                  </code>
                );
              },
              pre({ children, ...props }) {
                let language = 'text';
                Children.forEach(children, (child) => {
                  if (isValidElement<{ className?: string; 'data-language'?: string }>(child)) {
                    language =
                      child.props['data-language'] ??
                      /language-([\w-]+)/.exec(child.props.className ?? '')?.[1] ??
                      language;
                  }
                });

                return (
                  <div className={styles.codeBlock}>
                    <div className={styles.codeHeader}>
                      <span>{language}</span>
                    </div>
                    <pre {...props}>{children}</pre>
                  </div>
                );
              },
              table({ children, ...props }) {
                return (
                  <div className={styles.tableScroller}>
                    <table {...props}>{children}</table>
                  </div>
                );
              },
            }}
          >
            {text}
          </ReactMarkdown>
        ) : (
          <div className={styles.thinkingBody}>
            <span />
            <span />
            <span />
          </div>
        )}
      </div>
      {hasText && (
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
      )}
    </div>
  );
}
