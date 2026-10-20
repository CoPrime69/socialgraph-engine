import type { ReactNode } from 'react';
import { describeError, isApiError } from '../api/errors';
import { Button } from './Button';
import { Icon, type IconName } from './Icon';
import styles from './StatusViews.module.css';

interface EmptyStateProps {
  title: string;
  children?: ReactNode;
  icon?: IconName;
  action?: ReactNode;
  compact?: boolean;
}

export function EmptyState({ title, children, icon = 'info', action, compact }: EmptyStateProps) {
  return (
    <div className={[styles.state, compact ? styles.compact : ''].join(' ')}>
      <span className={styles.icon}>
        <Icon name={icon} size={compact ? 20 : 24} />
      </span>
      <p className={styles.title}>{title}</p>
      {children ? <div className={styles.body}>{children}</div> : null}
      {action ? <div className={styles.action}>{action}</div> : null}
    </div>
  );
}

interface ErrorStateProps {
  error: unknown;
  /** Overrides the generic title derived from the error. */
  title?: string;
  /** Overrides the generic message derived from the error. */
  message?: string;
  onRetry?: () => void;
  retrying?: boolean;
  compact?: boolean;
}

/** Error panel with a retry button. Announced to assistive technology via role="alert". */
export function ErrorState({
  error,
  title,
  message,
  onRetry,
  retrying = false,
  compact,
}: ErrorStateProps) {
  const description = describeError(error);
  const requestId = isApiError(error) ? error.requestId : null;
  return (
    <div
      className={[styles.state, styles.error, compact ? styles.compact : ''].join(' ')}
      role="alert"
    >
      <span className={styles.icon}>
        <Icon name="alert" size={compact ? 20 : 24} />
      </span>
      <p className={styles.title}>{title ?? description.title}</p>
      <p className={styles.body}>{message ?? description.message}</p>
      {requestId ? <p className={styles.reference}>Reference: {requestId}</p> : null}
      {onRetry ? (
        <div className={styles.action}>
          <Button
            icon="refresh"
            size="sm"
            onClick={onRetry}
            loading={retrying}
            loadingLabel="Retrying…"
          >
            Try again
          </Button>
        </div>
      ) : null}
    </div>
  );
}

interface LoadingStateProps {
  label: string;
  children: ReactNode;
}

/** Wraps skeleton content: marks the region busy and gives it a text alternative. */
export function LoadingState({ label, children }: LoadingStateProps) {
  return (
    <div aria-busy="true" className={styles.loading}>
      <span className="visually-hidden" role="status">
        {label}
      </span>
      {children}
    </div>
  );
}
