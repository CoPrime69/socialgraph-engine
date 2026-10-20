import { useRecommendations } from '../../api/queries';
import type { Recommendation } from '../../api/types';
import { hasStatus } from '../../api/errors';
import { Button, ButtonLink } from '../../components/Button';
import { Card } from '../../components/Card';
import { Meter } from '../../components/Meter';
import { Skeleton } from '../../components/Skeleton';
import { EmptyState, ErrorState, LoadingState } from '../../components/StatusViews';
import { UserChip } from '../../components/UserChip';
import { formatInteger, formatPercent } from '../../lib/format';
import { useConnect } from './connectionMutations';
import { explainRecommendation } from './explain';
import styles from './network.module.css';

export const RECOMMENDATION_LIMIT = 10;

export function RecommendationsCard() {
  const recommendations = useRecommendations(RECOMMENDATION_LIMIT);
  const data = recommendations.data;

  let body;
  if (recommendations.isPending) {
    body = (
      <LoadingState label="Loading suggestions…">
        <div className={styles.skeletonList}>
          {Array.from({ length: 4 }, (_, i) => (
            <Skeleton key={i} height="4.5rem" radius="10px" />
          ))}
        </div>
      </LoadingState>
    );
  } else if (recommendations.isError) {
    const settingUp = hasStatus(recommendations.error, 404);
    body = (
      <ErrorState
        compact
        error={recommendations.error}
        title={settingUp ? 'Your network is still being set up' : undefined}
        message={
          settingUp ? 'This usually takes a few seconds. Please try again shortly.' : undefined
        }
        onRetry={() => void recommendations.refetch()}
        retrying={recommendations.isFetching}
      />
    );
  } else if (data?.items.length === 0) {
    body = (
      <EmptyState
        compact
        title="No suggestions right now"
        action={
          <ButtonLink to="/people" size="sm" icon="search">
            Browse people
          </ButtonLink>
        }
      >
        You are connected to everyone we would suggest.
      </EmptyState>
    );
  } else if (data) {
    body = (
      <ul role="list" className={styles.list}>
        {data.items.map((recommendation) => (
          <RecommendationRow key={recommendation.user.id} recommendation={recommendation} />
        ))}
      </ul>
    );
  }

  return (
    <Card
      title="People you may know"
      description="Ranked by mutual connections, shared technologies and communities."
      busy={recommendations.isFetching && !recommendations.isPending}
      footer={data?.stale ? 'Updating with your latest changes…' : undefined}
    >
      {body}
    </Card>
  );
}

function RecommendationRow({ recommendation }: { recommendation: Recommendation }) {
  const connect = useConnect();
  const { user } = recommendation;
  const reasons = explainRecommendation(recommendation);
  return (
    <li className={styles.row}>
      <div className={styles.person}>
        <UserChip
          user={user}
          subtitle={`${user.branch} · ${formatInteger(user.reputation)} reputation`}
        />
        <ul role="list" className={styles.reasons} aria-label={`Why ${user.name} is suggested`}>
          {reasons.map((reason) => (
            <li key={reason}>{reason}</li>
          ))}
        </ul>
      </div>
      <div className={styles.strength}>
        <Meter
          size="sm"
          value={recommendation.score}
          label={`Match with ${user.name}`}
          valueText={formatPercent(recommendation.score)}
        />
        <p className={styles.since}>Match score</p>
      </div>
      <div className={styles.rowAction}>
        <Button
          size="sm"
          variant="primary"
          icon="userPlus"
          loading={connect.isPending}
          loadingLabel="Connecting…"
          aria-label={`Connect with ${user.name}`}
          onClick={() => {
            connect.mutate({ id: user.id, name: user.name });
          }}
        >
          Connect
        </Button>
      </div>
    </li>
  );
}
