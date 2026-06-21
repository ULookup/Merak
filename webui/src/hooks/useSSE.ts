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

  const [connState, setConnState] = useState<ConnectionState>('disconnected');

  useEffect(() => {
    if (!url) {
      setConnState('disconnected');
      return;
    }

    let cancelled = false;
    let retries = 0;
    let controller: AbortController | null = null;

    async function connect(after: number) {
      const target = url! + (url!.includes('?') ? '&' : '?') + 'after=' + after;
      controller = new AbortController();

      try {
        setConnState(retries === 0 ? 'connecting' : 'reconnecting');
        const res = await fetch(target, {
          headers: { Accept: 'text/event-stream' },
          signal: controller.signal,
        });

        if (!res.ok || !res.body) throw new Error('SSE connection failed');
        setConnState('connected');
        retries = 0;

        const reader = res.body.getReader();
        const decoder = new TextDecoder();
        let buffer = '';

        while (!cancelled) {
          const { done, value } = await reader.read();
          if (done) break;

          buffer += decoder.decode(value, { stream: true });
          const parts = buffer.split('\n\n');
          buffer = parts.pop() ?? '';

          for (const part of parts) {
            const frame = parseSseFrame(part);
            if (frame) {
              dispatchRef.current({ type: 'APPLY_SSE', frame });
            }
          }
        }
      } catch (err) {
        if ((err as Error).name === 'AbortError') return;
      }

      controller = null;

      if (!cancelled && retries < 10) {
        setConnState('reconnecting');
        const delay = Math.min(1000 * Math.pow(2, retries), 30000);
        retries++;
        if (!cancelled) setTimeout(() => connect(lastSeqRef.current), delay);
      } else if (!cancelled) {
        setConnState('disconnected');
      }
    }

    connect(lastSeq);

    return () => {
      cancelled = true;
      controller?.abort();
    };
  }, [url]);

  return connState;
}
