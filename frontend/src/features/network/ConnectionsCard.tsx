import { useState } from 'react';
import { useMyConnections } from '../../api/queries';
import type { Connection } from '../../api/types';
import { Button, ButtonLink } from '../../components/Button';
import { Card } from '../../components/Card';
import { Meter } from '../../components/Meter';
import { Pagination } from '../../components/Pagination';
import { Skeleton } from '../../components/Skeleton';
import { EmptyState, ErrorState, LoadingState } from '../../components/StatusViews';
import { UserChip } from '../../components/UserChip';
import { formatInteger, formatPercent } from '../../lib/format';
import { useDisconnect, type ConnectionTarget } from './connectionMutations';
import { describeConnection } from './explain';
import { RemoveConnectionDialog } from './RemoveConnectionDialog';
import styles from './network.module.css';

const PAGE_SIZE = 10;

export function ConnectionsCard() {
  const connections = useMyConnections();
  const disconnect = useDisconnect();
  const [removing, setRemoving] = useState<ConnectionTarget | null>(null);
  const [requestedPage, setPage] = useState(1);

  const items = connections.data?.items ?? [];
  const pageCount = Math.max(1, Math.ceil(items.length / PAGE_SIZE));
  const page = Math.min(requestedPage, pageCount);
  const visible = items.slice((page - 1) * PAGE_SIZE, page * PAGE_SIZE);

  let body;
  if (connections.isPending) {
    body = (
      <LoadingState label="Loading your connections…">
        <div className={styles.skeletonList}>
          {Array.from({ length: 4 }, (_, i) => (
            <Skeleton key={i} height="3.25rem" radius="10px" />
          ))}
        </div>
      </LoadingState>
    );
  } else if (connections.isError) {
    body = (
      <ErrorState
        compact
        error={connections.error}
        onRetry={() => void connections.refetch()}
        retrying={connections.isFetching}
      />
    );
  } else if (items.length === 0) {
    body = (
      <EmptyState
        compact
        icon="network"
        title="You haven't connected with anyone yet"
        action={
          <ButtonLink to="/people" size="sm" icon="search">
            Find people
          </ButtonLink>
        }
      >
        Connect with someone from the suggestions, or search for people you know.
      </EmptyState>
    );
  } else {
    body = (
      <>
        <ul role="list" className={styles.list}>
          {visible.map((connection) => (
            <ConnectionRow
              key={connection.user.id}
              connection={connection}
              onRemove={() => {
                setRemoving({ id: connection.user.id, name: connection.user.name });
              }}
            />
          ))}
        </ul>
        {items.length > PAGE_SIZE ? (
          <Pagination
            page={page}
            pageSize={PAGE_SIZE}
            total={items.length}
            onPageChange={setPage}
            noun="connections"
          />
        ) : null}
      </>
    );
  }

  return (
    <Card
      title="Your connections"
      description={
        connections.data
          ? `${formatInteger(items.length)} ${items.length === 1 ? 'person' : 'people'}, strongest connection first`
          : 'People you are connected to'
      }
    >
      {body}
      <RemoveConnectionDialog
        target={removing}
        pending={disconnect.isPending}
        onCancel={() => {
          setRemoving(null);
        }}
        onConfirm={(target) => {
          disconnect.mutate(target, {
            onSettled: () => {
              setRemoving(null);
            },
          });
        }}
      />
    </Card>
  );
}

function ConnectionRow({ connection, onRemove }: { connection: Connection; onRemove: () => void }) {
  const { user } = connection;
  return (
    <li className={styles.row}>
      <div className={styles.person}>
        <UserChip user={user} subtitle={`@${user.handle} · ${user.branch}`} />
      </div>
      <div className={styles.strength}>
        <Meter
          size="sm"
          value={connection.strength}
          label={`Connection strength with ${user.name}`}
          valueText={formatPercent(connection.strength)}
        />
        <p className={styles.since}>{describeConnection(connection)}</p>
      </div>
      <div className={styles.rowAction}>
        <Button
          size="sm"
          variant="ghost"
          icon="userMinus"
          aria-label={`Remove connection with ${user.name}`}
          onClick={onRemove}
        >
          Remove
        </Button>
      </div>
    </li>
  );
}
