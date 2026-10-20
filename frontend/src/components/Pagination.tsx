import { formatInteger } from '../lib/format';
import { Button } from './Button';
import styles from './Pagination.module.css';

interface PaginationProps {
  /** 1-based page number. */
  page: number;
  pageSize: number;
  total: number;
  onPageChange: (page: number) => void;
  /** What is being paged, e.g. "people". */
  noun?: string;
  disabled?: boolean;
}

export function Pagination({
  page,
  pageSize,
  total,
  onPageChange,
  noun = 'results',
  disabled = false,
}: PaginationProps) {
  const pageCount = Math.max(1, Math.ceil(total / pageSize));
  const first = total === 0 ? 0 : (page - 1) * pageSize + 1;
  const last = Math.min(total, page * pageSize);
  return (
    <nav className={styles.pagination} aria-label="Pagination">
      <p className={styles.summary}>
        Showing <strong>{formatInteger(first)}</strong>–<strong>{formatInteger(last)}</strong> of{' '}
        <strong>{formatInteger(total)}</strong> {noun}
      </p>
      <div className={styles.controls}>
        <Button
          size="sm"
          icon="chevronLeft"
          disabled={disabled || page <= 1}
          onClick={() => {
            onPageChange(page - 1);
          }}
        >
          Previous
        </Button>
        <span className={styles.page}>
          Page {formatInteger(page)} of {formatInteger(pageCount)}
        </span>
        <Button
          size="sm"
          disabled={disabled || page >= pageCount}
          onClick={() => {
            onPageChange(page + 1);
          }}
        >
          Next
        </Button>
      </div>
    </nav>
  );
}
