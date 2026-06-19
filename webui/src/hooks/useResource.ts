import { useCallback, useEffect, useReducer, useRef, useState } from 'react';

export type ResourceState<T> = {
  status: 'loading' | 'ready' | 'error';
  data: T | null;
  error: Error | null;
  retry(): void;
};

type ResourceSnapshot<T> = Omit<ResourceState<T>, 'retry'> & { key: string };

function toError(error: unknown): Error {
  return error instanceof Error ? error : new Error(String(error));
}

function isAbortError(error: unknown): boolean {
  return error instanceof DOMException
    ? error.name === 'AbortError'
    : error instanceof Error && error.name === 'AbortError';
}

export function useResource<T>(
  key: string,
  loader: (signal: AbortSignal) => Promise<T>,
): ResourceState<T> {
  const [snapshot, setSnapshot] = useState<ResourceSnapshot<T>>({
    key,
    status: 'loading',
    data: null,
    error: null,
  });
  const [retryVersion, retry] = useReducer((version: number) => version + 1, 0);
  const loaderRef = useRef(loader);
  const previousKeyRef = useRef(key);
  loaderRef.current = loader;

  useEffect(() => {
    const controller = new AbortController();
    const keyChanged = previousKeyRef.current !== key;
    previousKeyRef.current = key;

    setSnapshot((previous) => ({
      key,
      status: 'loading',
      data: keyChanged ? null : previous.data,
      error: null,
    }));

    void (async () => {
      try {
        const data = await loaderRef.current(controller.signal);
        if (!controller.signal.aborted) {
          setSnapshot({ key, status: 'ready', data, error: null });
        }
      } catch (error) {
        if (!controller.signal.aborted && !isAbortError(error)) {
          setSnapshot((previous) => ({
            key,
            status: 'error',
            data: previous.data,
            error: toError(error),
          }));
        }
      }
    })();

    return () => controller.abort();
  }, [key, retryVersion]);

  const retryResource = useCallback(() => retry(), []);
  if (snapshot.key !== key) {
    return { status: 'loading', data: null, error: null, retry: retryResource };
  }

  return {
    status: snapshot.status,
    data: snapshot.data,
    error: snapshot.error,
    retry: retryResource,
  };
}
