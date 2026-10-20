import { useSummary } from '../../api/queries';
import { useSession } from '../../auth/authContext';
import { PageHeader } from '../../components/PageHeader';
import { Skeleton } from '../../components/Skeleton';
import { StatGrid, StatTile } from '../../components/StatTile';
import { ErrorState, LoadingState } from '../../components/StatusViews';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import {
  formatDecimal,
  formatInteger,
  formatPercent,
  formatRelativeTime,
  formatSignificant,
  pluralize,
} from '../../lib/format';
import { GraphCard } from '../graph/GraphCard';
import { InfluencersCard } from './InfluencersCard';
import { TopCommunitiesCard } from './TopCommunitiesCard';
import styles from './OverviewPage.module.css';

/** Enough decimals that a share just below 100 % is never rounded up to "100%". */
function shareDigits(share: number): number {
  if (share >= 1 || share < 0.995) return 0;
  return share < 0.9995 ? 1 : 2;
}

export function OverviewPage() {
  useDocumentTitle('Overview');
  const session = useSession();
  const summary = useSummary();
  const data = summary.data;

  let tiles;
  if (summary.isPending) {
    tiles = (
      <LoadingState label="Loading network statistics…">
        <div className={styles.tileSkeletons}>
          {Array.from({ length: 4 }, (_, i) => (
            <Skeleton key={i} height="6rem" radius="10px" />
          ))}
        </div>
      </LoadingState>
    );
  } else if (!data) {
    tiles = (
      <ErrorState
        error={summary.error}
        title="Network statistics are unavailable"
        onRetry={() => void summary.refetch()}
        retrying={summary.isFetching}
      />
    );
  } else {
    const reach = data.users > 0 ? data.largest_component / data.users : 0;
    tiles = (
      <StatGrid label="Network statistics">
        <StatTile
          label="People"
          value={formatInteger(data.users)}
          detail="Stack Overflow users and members"
        />
        <StatTile
          label="Connections"
          value={formatInteger(data.connections)}
          detail={`${formatDecimal(data.average_degree, 1)} per person on average`}
          hint={`Density ${formatSignificant(data.density)}: the share of all possible connections that exist.`}
        />
        <StatTile
          label="Communities"
          value={formatInteger(data.communities)}
          detail={`Modularity ${formatDecimal(data.modularity, 2)}`}
          hint="Modularity measures how clearly the network splits into communities; above 0.3 is a clear structure."
        />
        <StatTile
          label="Largest connected group"
          value={formatPercent(reach, shareDigits(reach))}
          detail={`${formatInteger(data.largest_component)} people · ${pluralize(data.components, 'separate group')}`}
          hint="Share of people who can reach each other through a chain of connections."
        />
      </StatGrid>
    );
  }

  return (
    <div className={styles.page}>
      <PageHeader
        title="Network overview"
        description="How the Stack Overflow community of late 2008 is connected: its most influential people, its communities and the network itself."
        meta={
          data ? (
            <>
              Graph version {formatInteger(data.graph_version)} · analytics computed{' '}
              <time dateTime={data.computed_at}>{formatRelativeTime(data.computed_at)}</time> in{' '}
              {formatInteger(data.compute_ms)} ms
              {data.stale ? ' · updating…' : ''}
            </>
          ) : undefined
        }
      />
      {tiles}
      <div className={styles.columns}>
        <InfluencersCard
          youId={session.user.id}
          betweennessApproximate={data?.betweenness_approximate ?? false}
        />
        <TopCommunitiesCard totalUsers={data?.users} />
      </div>
      <GraphCard
        title="Network graph"
        description="The most influential people and how they connect. Colours show communities."
        scope={{ kind: 'top' }}
        youId={session.user.id}
      />
    </div>
  );
}
