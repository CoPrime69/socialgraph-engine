import { Link } from 'react-router';
import { formatInteger } from '../lib/format';
import styles from './TagChip.module.css';

interface TagChipProps {
  tag: string;
  /** Optional count shown after the tag, e.g. posts or questions. */
  count?: number;
  /** What `count` counts, read by screen readers ("posts"). */
  countLabel?: string;
  size?: 'sm' | 'md';
}

/** A Stack Overflow tag linking to its questions on the Topics page. */
export function TagChip({ tag, count, countLabel, size = 'md' }: TagChipProps) {
  return (
    <Link
      to={{ pathname: '/topics', search: `?tag=${encodeURIComponent(tag)}` }}
      className={`${styles.tag} ${styles[size]}`}
    >
      {tag}
      {count !== undefined ? (
        <>
          {' '}
          <span className={styles.count}>
            {formatInteger(count)}
            {countLabel ? (
              <>
                {' '}
                <span className="visually-hidden">{countLabel}</span>
              </>
            ) : null}
          </span>
        </>
      ) : null}
    </Link>
  );
}
