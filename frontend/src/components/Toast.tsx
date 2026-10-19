import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { IconButton } from './Button';
import { Icon, type IconName } from './Icon';
import { ToastContext, type ToastApi, type ToastTone } from './toastContext';
import styles from './Toast.module.css';

interface ToastItem {
  id: number;
  tone: ToastTone;
  title: string;
  description?: string | undefined;
}

const DURATION_MS: Record<ToastTone, number> = { success: 5000, info: 5000, error: 10000 };
const MAX_VISIBLE = 4;
const ICONS: Record<ToastTone, IconName> = { success: 'check', error: 'alert', info: 'info' };

let nextId = 1;

export function ToastProvider({ children }: { children: ReactNode }) {
  const [toasts, setToasts] = useState<ToastItem[]>([]);
  // Source of truth for timers/callbacks; `toasts` state mirrors it for rendering.
  const itemsRef = useRef<ToastItem[]>([]);
  const timers = useRef(new Map<number, ReturnType<typeof setTimeout>>());
  const paused = useRef(false);

  const clearTimer = useCallback((id: number) => {
    const timer = timers.current.get(id);
    if (timer !== undefined) clearTimeout(timer);
    timers.current.delete(id);
  }, []);

  const commit = useCallback((next: ToastItem[]) => {
    itemsRef.current = next;
    setToasts(next);
  }, []);

  const dismiss = useCallback(
    (id: number) => {
      clearTimer(id);
      commit(itemsRef.current.filter((toast) => toast.id !== id));
    },
    [clearTimer, commit],
  );

  const schedule = useCallback(
    (toast: ToastItem) => {
      if (paused.current || timers.current.has(toast.id)) return;
      timers.current.set(
        toast.id,
        setTimeout(() => {
          dismiss(toast.id);
        }, DURATION_MS[toast.tone]),
      );
    },
    [dismiss],
  );

  const show = useCallback(
    (tone: ToastTone, title: string, description?: string) => {
      const toast: ToastItem = { id: nextId++, tone, title, description };
      const next = [...itemsRef.current, toast];
      const overflow = Math.max(0, next.length - MAX_VISIBLE);
      for (const dropped of next.slice(0, overflow)) clearTimer(dropped.id);
      commit(next.slice(overflow));
      schedule(toast);
    },
    [clearTimer, commit, schedule],
  );

  const pause = useCallback(() => {
    paused.current = true;
    for (const timer of timers.current.values()) clearTimeout(timer);
    timers.current.clear();
  }, []);

  const resume = useCallback(() => {
    paused.current = false;
    for (const toast of itemsRef.current) schedule(toast);
  }, [schedule]);

  useEffect(() => {
    const pending = timers.current;
    return () => {
      for (const timer of pending.values()) clearTimeout(timer);
      pending.clear();
    };
  }, []);

  const api = useMemo<ToastApi>(
    () => ({
      success: (title, description) => {
        show('success', title, description);
      },
      error: (title, description) => {
        show('error', title, description);
      },
      info: (title, description) => {
        show('info', title, description);
      },
      dismiss,
    }),
    [show, dismiss],
  );

  const renderToast = (toast: ToastItem) => (
    <div key={toast.id} className={`${styles.toast} ${styles[toast.tone]}`}>
      <span className={styles.icon}>
        <Icon name={ICONS[toast.tone]} />
      </span>
      <div className={styles.text}>
        <p className={styles.title}>{toast.title}</p>
        {toast.description ? <p className={styles.description}>{toast.description}</p> : null}
      </div>
      <IconButton
        icon="close"
        label="Dismiss notification"
        size="sm"
        onClick={() => {
          dismiss(toast.id);
        }}
      />
    </div>
  );

  return (
    <ToastContext.Provider value={api}>
      {children}
      {/* Live regions exist before any toast is added so screen readers announce reliably. */}
      <div
        className={styles.viewport}
        onMouseEnter={pause}
        onMouseLeave={resume}
        onFocus={pause}
        onBlur={resume}
      >
        <div role="alert" aria-live="assertive" className={styles.region}>
          {toasts.filter((toast) => toast.tone === 'error').map(renderToast)}
        </div>
        <div role="status" aria-live="polite" className={styles.region}>
          {toasts.filter((toast) => toast.tone !== 'error').map(renderToast)}
        </div>
      </div>
    </ToastContext.Provider>
  );
}
