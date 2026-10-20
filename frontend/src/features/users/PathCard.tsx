import { hasStatus } from '../../api/errors';
import { usePath } from '../../api/queries';
import type { UserProfile } from '../../api/types';
import { Card } from '../../components/Card';
import { Icon } from '../../components/Icon';
import { Skeleton } from '../../components/Skeleton';
import { ErrorState, LoadingState } from '../../components/StatusViews';
import { UserChip } from '../../components/UserChip';
import styles from './users.module.css';

/** Degrees of separation between the signed-in user and `target`, as a chain of people. */
export function PathCard({ target, youId }: { target: UserProfile; youId: number }) {
  const path = usePath(target.id, true);
  const data = path.data;

  let body;
  if (path.isPending) {
    body = (
      <LoadingState label="Finding the shortest path…">
        <Skeleton height="2.5rem" radius="999px" />
      </LoadingState>
    );
  } else if (!data) {
    const settingUp = hasStatus(path.error, 404);
    body = (
      <ErrorState
        compact
        error={path.error}
        title={settingUp ? 'Your place in the network is still being set up' : undefined}
        message={
          settingUp ? 'This usually takes a few seconds. Please try again shortly.' : undefined
        }
        onRetry={() => void path.refetch()}
        retrying={path.isFetching}
      />
    );
  } else if (!data.found || data.degrees === null) {
    body = (
      <p className={styles.pathSummary}>
        You are not connected to {target.name} through the network yet. Connect directly, or with
        someone in their circle.
      </p>
    );
  } else {
    const degrees = data.degrees;
    body = (
      <>
        <p className={styles.pathSummary}>
          {degrees === 1 ? (
            <>You are directly connected to {target.name}.</>
          ) : (
            <>
              <strong className={styles.degrees}>{degrees}</strong> degrees of separation: the
              shortest chain of connections from you to {target.name}.
            </>
          )}
        </p>
        <ol className={styles.chain} aria-label={`Path from you to ${target.name}`}>
          {data.path.map((step, index) => (
            <li key={step.id} className={styles.chainStep}>
              {index > 0 ? (
                <span className={styles.chainArrow}>
                  <Icon name="arrowRight" size={16} />
                </span>
              ) : null}
              <UserChip user={step} variant="pill" isYou={step.id === youId} />
            </li>
          ))}
        </ol>
      </>
    );
  }

  return (
    <Card
      title="Degrees of separation"
      busy={path.isFetching && !path.isPending}
      footer={data?.stale ? 'Updating with your latest changes…' : undefined}
    >
      {body}
    </Card>
  );
}
