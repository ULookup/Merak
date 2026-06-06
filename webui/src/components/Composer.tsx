import { useCallback, useRef, useState } from 'react';
import { Send, Square } from 'lucide-react';
import { api } from '../api/client';
import { useAppState } from '../AppState';
import styles from './Composer.module.css';
import { useToast } from './Toast';

export default function Composer() {
  const { state } = useAppState();
  const { showToast } = useToast();
  const [text, setText] = useState('');
  const [sending, setSending] = useState(false);
  const ref = useRef<HTMLTextAreaElement>(null);

  const cancel = useCallback(async () => {
    if (!state.currentRun) return;
    try {
      await api.cancelRun(state.currentRun);
    } catch {
      /* ignore */
    }
  }, [state.currentRun]);

  const send = useCallback(async () => {
    const msg = text.trim();
    if (!msg || sending) return;
    if (!state.sessionId) {
      showToast('Waiting for session...', 'info');
      return;
    }
    setText('');
    setSending(true);

    try {
      await api.startRun(state.sessionId, msg, state.selectedModel);
    } catch (e) {
      showToast(`Error: ${e}`, 'error');
    } finally {
      setSending(false);
    }
  }, [text, sending, state.sessionId, state.selectedModel, showToast]);

  const isRunning =
    state.currentRun !== null && state.status !== 'idle' && state.status !== 'waiting_approval';

  function onKeyDown(e: React.KeyboardEvent) {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      send();
    }
  }

  return (
    <div className={styles.area}>
      <div className={styles.box}>
        <textarea
          ref={ref}
          className={styles.input}
          data-testid="composer-input"
          aria-label="Type a message"
          value={text}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={onKeyDown}
          placeholder="Type a message... Enter to send, Shift+Enter for newline"
          rows={2}
          disabled={state.status !== 'idle' && state.status !== 'waiting_approval'}
        />
        {isRunning ? (
          <button
            className={styles.cancelBtn}
            onClick={cancel}
            data-testid="cancel-btn"
            aria-label="Cancel run"
          >
            <Square size={14} aria-hidden="true" strokeWidth={2.4} />
            Cancel
          </button>
        ) : (
          <button
            className={styles.sendBtn}
            onClick={send}
            disabled={sending || !text.trim()}
            data-testid="send-btn"
            aria-label="Send message"
          >
            <Send size={14} aria-hidden="true" strokeWidth={2.4} />
            Send
          </button>
        )}
      </div>
      <div className={styles.hint}>
        Enter &middot; send &nbsp;|&nbsp; Shift+Enter &middot; newline
      </div>
    </div>
  );
}
