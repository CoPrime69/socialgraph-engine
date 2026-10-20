import { Skeleton } from './Skeleton';
import { LoadingState } from './StatusViews';
import styles from './PageSkeleton.module.css';

/** Placeholder shown while a page's code or first data is loading. */
export function PageSkeleton({ label = 'Loading page…' }: { label?: string }) {
  return (
    <LoadingState label={label}>
      <div className={styles.page}>
        <Skeleton width="14rem" height="2rem" />
        <Skeleton width="24rem" height="1rem" />
        <div className={styles.tiles}>
          {Array.from({ length: 4 }, (_, i) => (
            <Skeleton key={i} height="5.5rem" radius="10px" />
          ))}
        </div>
        <Skeleton height="18rem" radius="14px" />
      </div>
    </LoadingState>
  );
}
