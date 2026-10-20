import { useCallback, useEffect, useRef } from 'react';

/**
 * Returns a stable function that invokes `callback` after `delayMs` of inactivity.
 * Pending calls are cancelled on unmount; `flush`/`cancel` are exposed for explicit control.
 */
export function useDebouncedCallback<Args extends unknown[]>(
  callback: (...args: Args) => void,
  delayMs: number,
) {
  const callbackRef = useRef(callback);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const pendingArgs = useRef<Args | null>(null);

  useEffect(() => {
    callbackRef.current = callback;
  }, [callback]);

  const cancel = useCallback(() => {
    if (timerRef.current !== null) clearTimeout(timerRef.current);
    timerRef.current = null;
    pendingArgs.current = null;
  }, []);

  const flush = useCallback(() => {
    if (timerRef.current === null || pendingArgs.current === null) return;
    const args = pendingArgs.current;
    cancel();
    callbackRef.current(...args);
  }, [cancel]);

  const debounced = useCallback(
    (...args: Args) => {
      if (timerRef.current !== null) clearTimeout(timerRef.current);
      pendingArgs.current = args;
      timerRef.current = setTimeout(() => {
        timerRef.current = null;
        pendingArgs.current = null;
        callbackRef.current(...args);
      }, delayMs);
    },
    [delayMs],
  );

  useEffect(() => cancel, [cancel]);

  return { debounced, cancel, flush };
}
