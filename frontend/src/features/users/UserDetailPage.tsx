import { useParams } from 'react-router';
import { hasStatus } from '../../api/errors';
import { useUser } from '../../api/queries';
import { useSession } from '../../auth/authContext';
import { PageSkeleton } from '../../components/PageSkeleton';
import { ErrorState } from '../../components/StatusViews';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { NotFoundPage } from '../errors/NotFoundPage';
import { GraphCard } from '../graph/GraphCard';
import { ActivityCard } from './ActivityCard';
import { MetricsCard } from './MetricsCard';
import { PathCard } from './PathCard';
import { ProfileFacts, ProfileHeader } from './ProfileSummary';
import styles from './users.module.css';

/** Ego-network size on a profile page (most influential neighbours first). */
const PROFILE_GRAPH_LIMIT = 150;

function parseUserId(value: string | undefined): number | null {
  if (value === undefined || !/^[1-9]\d{0,15}$/.test(value)) return null;
  const id = Number(value);
  return Number.isSafeInteger(id) ? id : null;
}

/** `/users/:userId`: validates the id and gives each person a fresh page state. */
export function UserDetailRoute() {
  const { userId } = useParams();
  const id = parseUserId(userId);
  if (id === null) {
    return <NotFoundPage title="Person not found" message="That profile address is not valid." />;
  }
  return <UserDetailPage key={id} userId={id} />;
}

export function UserDetailPage({ userId }: { userId: number }) {
  const session = useSession();
  const isMe = userId === session.user.id;
  const user = useUser(userId);
  useDocumentTitle(user.data?.name ?? 'Profile');

  if (user.isPending) return <PageSkeleton label="Loading profile…" />;
  if (user.isError) {
    if (hasStatus(user.error, 404)) {
      return (
        <NotFoundPage
          title="Person not found"
          message="There is no one with this id in the network."
        />
      );
    }
    return (
      <ErrorState
        error={user.error}
        title="We couldn't load this profile"
        onRetry={() => void user.refetch()}
        retrying={user.isFetching}
      />
    );
  }

  const profile = user.data;
  return (
    <div className={styles.page}>
      <ProfileHeader profile={profile} isMe={isMe} />
      <div className={styles.columns}>
        <div className={styles.main}>
          {isMe ? null : <PathCard target={profile} youId={session.user.id} />}
          <ActivityCard userId={profile.id} name={profile.name} source={profile.source} />
        </div>
        <div className={styles.side}>
          <ProfileFacts profile={profile} />
          <MetricsCard userId={profile.id} />
        </div>
      </div>
      <GraphCard
        title={isMe ? 'Your network graph' : `${profile.name}'s network`}
        description="Direct connections and how they connect to each other."
        scope={{ kind: 'ego', focusId: profile.id, limit: PROFILE_GRAPH_LIMIT }}
        youId={session.user.id}
        size="md"
        emptyMessage={
          isMe
            ? 'Connect with people to see your network here.'
            : `${profile.name} has no connections yet.`
        }
      />
    </div>
  );
}
