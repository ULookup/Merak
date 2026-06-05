import { useCallback, useRef, useState } from 'react';
import { api } from '../api/client';
import { useAppState } from '../AppState';
import styles from './Composer.module.css';

export default function Composer() {
  const { state, dispatch } = useAppState();
  const [text, setText] = useState('');
  const [sending, setSending] = useState(false);
  const ref = useRef<HTMLTextAreaElement>(null);

  const send = useCallback(async () => {
    const msg = text.trim();
    if (!msg || sending || !state.sessionId) return;
    setText('');
    setSending(true);

    try {
      await api.startRun(state.sessionId, msg, state.selectedModel);
    } catch (e) {
      dispatch({
        type: 'APPEND_MESSAGE',
        message: { id: 'err_' + Date.now(), kind: 'system', text: `Error: ${e}`, error: true },
      });
    } finally {
      setSending(false);
    }
  }, [text, sending, state.sessionId, state.selectedModel, dispatch]);

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
          value={text}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={onKeyDown}
          placeholder="Type a message... Enter to send, Shift+Enter for newline"
          rows={2}
          disabled={state.status !== 'idle' && state.status !== 'waiting_approval'}
        />
        <button className={styles.sendBtn} onClick={send} disabled={sending || !text.trim()} data-testid="send-btn">
          Send
        </button>
      </div>
      <div className={styles.hint}>
        Enter &middot; send &nbsp;|&nbsp; Shift+Enter &middot; newline
      </div>
    </div>
  );
}
