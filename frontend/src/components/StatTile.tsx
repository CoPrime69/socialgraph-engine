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

export function StatGrid({ children, label }: { children: ReactNode; label: string }) {
  return (
    <dl className={styles.grid} aria-label={label}>
      {children}
    </dl>
  );
}
