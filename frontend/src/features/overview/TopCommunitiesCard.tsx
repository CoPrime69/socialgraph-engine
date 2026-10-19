import { Link } from 'react-router';
import { useCommunities } from '../../api/queries';
import { Card } from '../../components/Card';
import { Skeleton } from '../../components/Skeleton';
import { EmptyState, ErrorState, LoadingState } from '../../components/StatusViews';
import { formatInteger, formatPercent } from '../../lib/format';
import { assignCommunitySlots, communityLabel } from '../graph/palette';
import styles from './OverviewPage.module.css';

const SHOWN = 6;

export function TopCommunitiesCard({ totalUsers }: { totalUsers: number | undefined }) {
  const communities = useCommunities();
  const items = communities.data?.items ?? [];
  const slots = assignCommunitySlots(items.map(({ id, size }) => ({ id, size })));

  let body;
  if (communities.isPending) {
    body = (
      <LoadingState label="Loading communities…">
        <div className={styles.skeletonRows}>
          {Array.from({ length: 5 }, (_, i) => (
            <Skeleton key={i} height="2rem" />
          ))}
        </div>
      </LoadingState>
    );
  } else if (communities.isError) {
    body = (
      <ErrorState
        compact
        error={communities.error}
        onRetry={() => void communities.refetch()}
        retrying={communities.isFetching}
      />
    );
  } else if (items.length === 0) {
    body = <EmptyState compact title="No communities yet" />;
  } else {
    body = (
      <ol className={styles.communityList}>
        {items.slice(0, SHOWN).map((community) => (
          <li key={community.id}>
            <Link
              to={`/communities?highlight=${String(community.id)}`}
              className={styles.communityLink}
            >
              <span className="community-swatch" data-slot={slots.get(community.id) ?? 0} />
              <span className={styles.communityName}>
                {communityLabel(community.id)}
                <span className={styles.communityMeta}>
                  {community.dominant_branch} · {community.dominant_category}
                </span>
              </span>
              <span className={styles.communitySize}>
                {formatInteger(community.size)}
                {totalUsers ? (
                  <span className={styles.communityShare}>
                    {formatPercent(community.size / totalUsers, 1)}
                  </span>
                ) : null}
              </span>
            </Link>
          </li>
        ))}
      </ol>
    );
  }

  return (
    <Card
      title="Largest communities"
      description={
        communities.data
          ? `${formatInteger(items.length)} communities found by the Louvain method.`
          : 'Groups that interact more with each other than with everyone else.'
      }
      footer={
        items.length > 0 ? (
          <Link to="/communities" className={styles.footerLink}>
            See all communities
          </Link>
        ) : undefined
      }
    >
      {body}
    </Card>
  );
}
