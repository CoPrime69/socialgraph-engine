import { useState } from 'react';
import { useMyConnections } from '../../api/queries';
import { Button } from '../../components/Button';
import { useConnect, useDisconnect, type ConnectionTarget } from './connectionMutations';
import { RemoveConnectionDialog } from './RemoveConnectionDialog';

/** Connect / Remove connection button for a profile page. */
export function ConnectionToggle({ target }: { target: ConnectionTarget }) {
  const connections = useMyConnections();
  const connect = useConnect();
  const disconnect = useDisconnect();
  const [confirming, setConfirming] = useState(false);

  if (connections.isPending) {
    return (
      <Button disabled loading loadingLabel="Checking…">
        Connect
      </Button>
    );
  }
  const connected = connections.data?.items.some((item) => item.user.id === target.id) ?? false;
  if (!connected) {
    return (
      <Button
        variant="primary"
        icon="userPlus"
        loading={connect.isPending}
        loadingLabel="Connecting…"
        onClick={() => {
          connect.mutate(target);
        }}
      >
        Connect
      </Button>
    );
  }
  return (
    <>
      <Button
        icon="userMinus"
        onClick={() => {
          setConfirming(true);
        }}
      >
        Remove connection
      </Button>
      <RemoveConnectionDialog
        target={confirming ? target : null}
        pending={disconnect.isPending}
        onCancel={() => {
          setConfirming(false);
        }}
        onConfirm={(value) => {
          disconnect.mutate(value, {
            onSettled: () => {
              setConfirming(false);
            },
          });
        }}
      />
    </>
  );
}
