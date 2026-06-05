import { useEffect, useRef } from 'react';
import type { Dispatch } from 'react';
import type { SseFrame } from '../api/types';
import type { Action } from '../AppState';

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

  useEffect(() => {
    if (!url) return;

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
          const delay = Math.min(1000 * Math.pow(2, retries), 30000);
          retries++;
          setTimeout(() => connect(lastSeqRef.current), delay);
        }
      };

      es.onopen = () => {
        retries = 0;
      };
    }

    connect(lastSeq);

    return () => {
      es?.close();
    };
  }, [url]);
}
