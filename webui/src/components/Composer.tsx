import { useCallback, useRef, useState } from 'react';
import { api } from '../api/client';
import { useAppState } from '../AppState';

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

    dispatch({
      type: 'APPEND_MESSAGE',
      message: { id: 'usr_' + Date.now(), kind: 'user', text: msg },
    });

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
    <div className="composer-area">
      <div className="composer-box">
        <textarea
          ref={ref}
          className="composer-input"
          value={text}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={onKeyDown}
          placeholder="Type a message... Enter to send, Shift+Enter for newline"
          rows={2}
          disabled={state.status !== 'idle' && state.status !== 'waiting_approval'}
        />
        <button className="send-btn" onClick={send} disabled={sending || !text.trim()}>
          Send
        </button>
      </div>
      <div className="composer-hint">
        Enter &middot; send &nbsp;|&nbsp; Shift+Enter &middot; newline
      </div>
    </div>
  );
}
