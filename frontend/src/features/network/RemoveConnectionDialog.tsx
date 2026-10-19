import { ConfirmDialog } from '../../components/ConfirmDialog';
import type { ConnectionTarget } from './connectionMutations';

interface RemoveConnectionDialogProps {
  target: ConnectionTarget | null;
  pending: boolean;
  onConfirm: (target: ConnectionTarget) => void;
  onCancel: () => void;
}

/** Confirmation before removing a connection (destructive: strength history is lost). */
export function RemoveConnectionDialog({
  target,
  pending,
  onConfirm,
  onCancel,
}: RemoveConnectionDialogProps) {
  return (
    <ConfirmDialog
      open={target !== null}
      title={target ? `Remove ${target.name} from your network?` : 'Remove connection?'}
      description={
        <p>
          You will no longer be connected. Influence scores, communities and recommendations update
          within a few seconds. You can connect again later.
        </p>
      }
      confirmLabel="Remove connection"
      pendingLabel="Removing…"
      tone="danger"
      pending={pending}
      onConfirm={() => {
        if (target) onConfirm(target);
      }}
      onCancel={onCancel}
    />
  );
}
