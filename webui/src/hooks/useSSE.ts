import { useEffect, useRef, useState } from 'react';
import type { Dispatch } from 'react';
import type { SseFrame } from '../api/types';
import type { Action } from '../AppState';

export type ConnectionState = 'connecting' | 'connected' | 'reconnecting' | 'disconnected';

function parseSseFrame(raw: string): SseFrame | null {
  let seq = 0;
  let type = '';
  let data = '';
  for (const line of raw.split('\n')) {
    if (line.startsWith('id: ')) seq = Number(line.slice(4));
    else if (line.startsWith('event: ')) type = line.slice(7);
    else if (line.startsWith('data: ')) data += line.slice(6);
  }
  if (!type || !data) return null;
  try {
    return { seq, type, payload: JSON.parse(data) };
  } catch {
    return null;
  }
}

export function useSSE(url: string | null, dispatch: Dispatch<Action>, lastSeq: number) {
  const dispatchRef = useRef(dispatch);
  dispatchRef.current = dispatch;
  const lastSeqRef = useRef(lastSeq);
  lastSeqRef.current = lastSeq;

  const [connState, setConnState] = useState<ConnectionState>(
    url ? 'connecting' : 'disconnected',
  );

  if (!url) return connState;

  setConnState('connecting');

  useEffect(() => {
    if (!url) return;

    let cancelled = false;
    let retries = 0;
    let es: EventSource | null = null;

    function connect(after: number) {
      const target = url! + (url!.includes('?') ? '&' : '?') + 'after=' + after;
      es = new EventSource(target);

      es.onmessage = (e) => {
        const frame = parseSseFrame(e.data);
        if (frame) {
          dispatchRef.current({ type: 'APPLY_SSE', frame });
          dispatchRef.current({ type: 'SET_LAST_SEQ', seq: frame.seq });
        }
      };

      es.onerror = () => {
        es?.close();
        if (retries < 10) {
          if (!cancelled) setConnState('reconnecting');
          const delay = Math.min(1000 * Math.pow(2, retries), 30000);
          retries++;
          if (!cancelled) setTimeout(() => connect(lastSeqRef.current), delay);
        } else {
          if (!cancelled) setConnState('disconnected');
        }
      };

      es.onopen = () => {
        retries = 0;
        if (!cancelled) setConnState('connected');
      };
    }

    connect(lastSeq);

    return () => {
      cancelled = true;
      es?.close();
    };
  }, [url]);

  return connState;
}
