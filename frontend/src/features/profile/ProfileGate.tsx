import type { ReactNode } from 'react';
import { hasStatus } from '../../api/errors';
import { PageSkeleton } from '../../components/PageSkeleton';
import { Spinner } from '../../components/Spinner';
import { ErrorState } from '../../components/StatusViews';
import { useMyProfile } from './useMyProfile';
import styles from './ProfileGate.module.css';

/**
 * Renders its children only once the caller's graph profile exists. Right after
 * registration the profile is still being provisioned (404), which shows a friendly
 * "Setting up your profile…" state while useMyProfile polls.
 */
export function ProfileGate({ children }: { children: ReactNode }) {
  const profile = useMyProfile();

  if (profile.data) return children;

  if (profile.isError) {
    if (hasStatus(profile.error, 404)) {
      return (
        <ErrorState
          error={profile.error}
          title="Your profile isn't ready yet"
          message="We couldn't finish setting up your profile. It may still be in progress, so please try again in a moment."
          onRetry={() => void profile.refetch()}
          retrying={profile.isFetching}
        />
      );
    }
    return (
      <ErrorState
        error={profile.error}
        title="We couldn't load your profile"
        onRetry={() => void profile.refetch()}
        retrying={profile.isFetching}
      />
    );
  }

  if (hasStatus(profile.failureReason, 404)) {
    return (
      <div className={styles.provisioning} role="status" aria-live="polite">
        <Spinner size={28} />
        <p className={styles.title}>Setting up your profile…</p>
        <p className={styles.body}>
          Your account was created. We're adding you to the network, which usually takes a few
          seconds.
        </p>
      </div>
    );
  }

  return <PageSkeleton label="Loading your profile…" />;
}
