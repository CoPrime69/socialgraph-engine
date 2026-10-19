import { useState } from 'react';
import { useInfluencers } from '../../api/queries';
import type { InfluenceMetric } from '../../api/types';
import { Card } from '../../components/Card';
import table from '../../components/DataTable.module.css';
import { Meter } from '../../components/Meter';
import { SegmentedControl } from '../../components/SegmentedControl';
import { Skeleton } from '../../components/Skeleton';
import { EmptyState, ErrorState, LoadingState } from '../../components/StatusViews';
import { UserChip } from '../../components/UserChip';
import { formatInteger, formatMetricScore } from '../../lib/format';
import styles from './OverviewPage.module.css';

const INFLUENCER_LIMIT = 10;

const METRICS: readonly { value: InfluenceMetric; label: string; explanation: string }[] = [
  {
    value: 'pagerank',
    label: 'PageRank',
    explanation: 'Influence earned by being connected to other influential people.',
  },
  {
    value: 'betweenness',
    label: 'Betweenness',
    explanation: 'How often someone sits on the shortest path between two others: the brokers.',
  },
  {
    value: 'closeness',
    label: 'Closeness',
    explanation: 'How few steps someone needs, on average, to reach everyone else.',
  },
  {
    value: 'degree',
    label: 'Connections',
    explanation: 'The share of all people someone is directly connected to.',
  },
];

interface InfluencersCardProps {
  youId: number;
  /** analytics-service samples betweenness on large graphs (summary.betweenness_approximate). */
  betweennessApproximate: boolean;
}

export function InfluencersCard({ youId, betweennessApproximate }: InfluencersCardProps) {
  const [metric, setMetric] = useState<InfluenceMetric>('pagerank');
  const influencers = useInfluencers(metric, INFLUENCER_LIMIT);
  const current = METRICS.find((option) => option.value === metric) ?? METRICS[0];
  const data = influencers.data;
  // While a new metric loads, the previous metric's rows stay visible (dimmed).
  const shownMetric = data?.metric ?? metric;
  const top = data?.items[0]?.score ?? 0;

  let body;
  if (influencers.isPending) {
    body = (
      <LoadingState label="Loading the most influential people…">
        <div className={styles.skeletonRows}>
          {Array.from({ length: 6 }, (_, i) => (
            <Skeleton key={i} height="2.25rem" />
          ))}
        </div>
      </LoadingState>
    );
  } else if (influencers.isError && !data) {
    body = (
      <ErrorState
        error={influencers.error}
        onRetry={() => void influencers.refetch()}
        retrying={influencers.isFetching}
      />
    );
  } else if (!data || data.items.length === 0) {
    body = <EmptyState title="No one to rank yet">The network has no members yet.</EmptyState>;
  } else {
    body = (
      <table role="table" className={`${table.table} ${table.stack}`}>
        <caption className="visually-hidden">
          Most influential people by {current?.label ?? metric}
        </caption>
        <thead role="rowgroup">
          <tr role="row">
            <th role="columnheader" scope="col" className={table.rank}>
              Rank
            </th>
            <th role="columnheader" scope="col">
              Person
            </th>
            <th role="columnheader" scope="col" className={table.numeric}>
              Reputation
            </th>
            <th role="columnheader" scope="col" className={table.scoreCell}>
              {METRICS.find((option) => option.value === shownMetric)?.label ?? 'Score'}
            </th>
          </tr>
        </thead>
        <tbody role="rowgroup">
          {data.items.map((item) => (
            <tr role="row" key={item.user.id}>
              <td role="cell" data-label="Rank" className={table.rank}>
                #{item.rank}
              </td>
              <td role="cell" data-primary="">
                <UserChip
                  user={item.user}
                  isYou={item.user.id === youId}
                  subtitle={`@${item.user.handle} · ${item.user.branch}`}
                />
              </td>
              <td role="cell" data-label="Reputation" className={table.numeric}>
                {formatInteger(item.user.reputation)}
              </td>
              <td role="cell" data-label="Score" className={table.scoreCell}>
                <Meter
                  size="sm"
                  value={top > 0 ? item.score / top : 0}
                  label={`${item.user.name}'s score relative to the top score`}
                  valueText={formatMetricScore(shownMetric, item.score)}
                />
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    );
  }

  return (
    <Card
      title="Most influential people"
      description={
        <>
          {current?.explanation}
          {metric === 'betweenness' && betweennessApproximate
            ? ' Estimated from a sample of the network.'
            : null}
        </>
      }
      toolbar={
        <SegmentedControl
          label="Influence measure"
          size="sm"
          options={METRICS}
          value={metric}
          onChange={setMetric}
        />
      }
      flush={data !== undefined && data.items.length > 0}
      busy={influencers.isPlaceholderData && influencers.isFetching}
    >
      {body}
    </Card>
  );
}
