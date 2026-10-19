import { useCallback, useEffect, useState } from 'react';

/** Seconds remaining until a deadline (e.g. a 429 Retry-After); 0 when inactive. */
export function useCountdown() {
  const [deadline, setDeadline] = useState<number | null>(null);
  const [now, setNow] = useState(0);

  useEffect(() => {
    if (deadline === null) return;
    const timer = setInterval(() => {
      const current = Date.now();
      setNow(current);
      if (current >= deadline) setDeadline(null);
    }, 1000);
    return () => {
      clearInterval(timer);
    };
  }, [deadline]);

  const start = useCallback((seconds: number) => {
    const current = Date.now();
    setNow(current);
    setDeadline(current + Math.max(0, seconds) * 1000);
  }, []);

  const remaining = deadline === null ? 0 : Math.max(0, Math.ceil((deadline - now) / 1000));
  return { remaining, start };
}
