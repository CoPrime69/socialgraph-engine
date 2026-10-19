import { Link } from 'react-router';
import { hasStatus } from '../../api/errors';
import { useSummary, useUserMetrics } from '../../api/queries';
import type { InfluenceMetric } from '../../api/types';
import { Card } from '../../components/Card';
import { Skeleton } from '../../components/Skeleton';
import { ErrorState, LoadingState } from '../../components/StatusViews';
import { formatInteger, formatMetricScore, ordinalRank } from '../../lib/format';
import { communityLabel } from '../graph/palette';
import { useCommunitySlots } from '../graph/useCommunitySlots';
import styles from './users.module.css';

const ROWS: readonly { metric: InfluenceMetric; label: string; hint: string }[] = [
  { metric: 'degree', label: 'Connections', hint: 'Direct connections' },
  { metric: 'pagerank', label: 'PageRank', hint: 'Connected to influential people' },
  { metric: 'betweenness', label: 'Betweenness', hint: 'Bridges between groups' },
  { metric: 'closeness', label: 'Closeness', hint: 'Few steps to everyone' },
];

/** Network position: centrality scores with ranks, and the person's community. */
export function MetricsCard({ userId }: { userId: number }) {
  const metrics = useUserMetrics(userId);
  const summary = useSummary();
  const slots = useCommunitySlots(undefined);
  const total = summary.data?.users;
  const data = metrics.data;

  let body;
  if (metrics.isPending) {
    body = (
      <LoadingState label="Loading network metrics…">
        <div className={styles.skeletonRows}>
          {Array.from({ length: 5 }, (_, i) => (
            <Skeleton key={i} height="1.75rem" />
          ))}
        </div>
      </LoadingState>
    );
  } else if (!data) {
    const settingUp = hasStatus(metrics.error, 404);
    body = (
      <ErrorState
        compact
        error={metrics.error}
        title={settingUp ? 'Metrics are still being calculated' : undefined}
        message={
          settingUp ? 'New members appear in the analytics within a few seconds.' : undefined
        }
        onRetry={() => void metrics.refetch()}
        retrying={metrics.isFetching}
      />
    );
  } else {
    const value = (metric: InfluenceMetric) =>
      metric === 'degree' ? formatInteger(data.degree) : formatMetricScore(metric, data[metric]);
    body = (
      <dl className={styles.metrics}>
        {ROWS.map((row) => (
          <div key={row.metric} className={styles.metricRow}>
            <dt>
              {row.label}
              <span className={styles.metricHint}>{row.hint}</span>
            </dt>
            <dd>
              <span className={styles.metricValue}>{value(row.metric)}</span>
              <span className={styles.metricRank}>
                Rank {ordinalRank(data.ranks[row.metric])}
                {total ? ` of ${formatInteger(total)}` : ''}
              </span>
            </dd>
          </div>
        ))}
        <div className={styles.metricRow}>
          <dt>
            Community
            <span className={styles.metricHint}>Found by the Louvain method</span>
          </dt>
          <dd>
            <Link
              to={`/communities?highlight=${String(data.community)}`}
              className={styles.communityLink}
            >
              <span className="community-swatch" data-slot={slots.get(data.community) ?? 0} />
              {communityLabel(data.community)}
            </Link>
          </dd>
        </div>
      </dl>
    );
  }

  return (
    <Card
      title="Position in the network"
      busy={metrics.isFetching && !metrics.isPending}
      footer={data ? `Graph version ${formatInteger(data.graph_version)}` : undefined}
    >
      {body}
    </Card>
  );
}
