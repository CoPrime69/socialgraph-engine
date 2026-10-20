import type { ReactNode } from 'react';
import styles from './StatTile.module.css';

export interface StatTileProps {
  label: string;
  value: ReactNode;
  /** Secondary line under the value. */
  detail?: ReactNode;
  /** Full explanation available on hover/focus and to screen readers. */
  hint?: string;
}

/** One term/description pair inside a <StatGrid> (a <dl>). */
export function StatTile({ label, value, detail, hint }: StatTileProps) {
  return (
    <div className={styles.tile} title={hint}>
      <dt className={styles.label}>{label}</dt>
      <dd className={styles.value}>{value}</dd>
      {detail ? <dd className={styles.detail}>{detail}</dd> : null}
      {hint ? <dd className="visually-hidden">{hint}</dd> : null}
    </div>
  );
}

interface StatGridProps {
  children: ReactNode;
  label: string;
  /** `row`: figures side by side; `key`: a vertical list, like the key beside a map. */
  layout?: 'row' | 'key';
}

export function StatGrid({ children, label, layout = 'row' }: StatGridProps) {
  return (
    <dl className={[styles.grid, styles[layout]].join(' ')} aria-label={label}>
      {children}
    </dl>
  );
}
