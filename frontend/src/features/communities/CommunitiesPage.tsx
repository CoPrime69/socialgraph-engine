import { useState } from 'react';
import { useSearchParams } from 'react-router';
import { useCommunities, useUserMetrics } from '../../api/queries';
import type { Community } from '../../api/types';
import { useSession } from '../../auth/authContext';
import { Badge } from '../../components/Badge';
import { Button } from '../../components/Button';
import { SelectField } from '../../components/Field';
import { PageHeader } from '../../components/PageHeader';
import { Pagination } from '../../components/Pagination';
import { Skeleton } from '../../components/Skeleton';
import { EmptyState, ErrorState, LoadingState } from '../../components/StatusViews';
import { UserChip } from '../../components/UserChip';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { usePrefersReducedMotion } from '../../hooks/useMediaQuery';
import { formatDecimal, formatInteger, formatPercent } from '../../lib/format';
import { GraphCard } from '../graph/GraphCard';
import { assignCommunitySlots, communityLabel, type PaletteSlot } from '../graph/palette';
import styles from './CommunitiesPage.module.css';

const PAGE_SIZE = 12;
const MEMBERS_SHOWN = 8;
const GRAPH_ID = 'community-graph';

function parseCommunityId(value: string | null): number | null {
  return value !== null && /^\d{1,9}$/.test(value) ? Number(value) : null;
}

export function CommunitiesPage() {
  useDocumentTitle('Communities');
  const session = useSession();
  const reducedMotion = usePrefersReducedMotion();
  const [searchParams, setSearchParams] = useSearchParams();
  const highlight = parseCommunityId(searchParams.get('highlight'));
  const communities = useCommunities();
  const myMetrics = useUserMetrics(session.user.id);
  const [requestedPage, setPage] = useState(1);

  const items = communities.data?.items ?? [];
  const total = items.reduce((sum, community) => sum + community.size, 0);
  const slots = assignCommunitySlots(items.map(({ id, size }) => ({ id, size })));
  const pageCount = Math.max(1, Math.ceil(items.length / PAGE_SIZE));
  const page = Math.min(requestedPage, pageCount);
  const visible = items.slice((page - 1) * PAGE_SIZE, page * PAGE_SIZE);
  const myCommunity = myMetrics.data?.community ?? null;

  const setHighlight = (id: number | null) => {
    setSearchParams(
      (previous) => {
        const next = new URLSearchParams(previous);
        if (id === null) next.delete('highlight');
        else next.set('highlight', String(id));
        return next;
      },
      { replace: true, preventScrollReset: true },
    );
  };

  const showInGraph = (id: number) => {
    setHighlight(id);
    const graph = document.getElementById(GRAPH_ID);
    graph?.scrollIntoView({ behavior: reducedMotion ? 'auto' : 'smooth', block: 'start' });
    graph?.querySelector<HTMLElement>('h2')?.focus({ preventScroll: true });
  };

  let list;
  if (communities.isPending) {
    list = (
      <LoadingState label="Loading communities…">
        <div className={styles.grid}>
          {Array.from({ length: 6 }, (_, i) => (
            <Skeleton key={i} height="14rem" radius="14px" />
          ))}
        </div>
      </LoadingState>
    );
  } else if (communities.isError) {
    list = (
      <ErrorState
        error={communities.error}
        onRetry={() => void communities.refetch()}
        retrying={communities.isFetching}
      />
    );
  } else if (items.length === 0) {
    list = (
      <EmptyState title="No communities yet" icon="communities">
        Communities appear once people are connected.
      </EmptyState>
    );
  } else {
    list = (
      <>
        <ul role="list" className={styles.grid}>
          {visible.map((community) => (
            <CommunityCard
              key={community.id}
              community={community}
              slot={slots.get(community.id) ?? 0}
              share={total > 0 ? community.size / total : 0}
              youId={session.user.id}
              isMine={community.id === myCommunity}
              highlighted={community.id === highlight}
              onShowInGraph={() => {
                showInGraph(community.id);
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
            noun="communities"
          />
        ) : null}
      </>
    );
  }

  return (
    <div className={styles.page}>
      <PageHeader
        title="Communities"
        description="Groups of people who interact more with each other than with the rest of the network, found with the Louvain method."
        meta={
          communities.data
            ? `${formatInteger(items.length)} communities · modularity ${formatDecimal(communities.data.modularity, 2)}`
            : undefined
        }
      />
      <GraphCard
        id={GRAPH_ID}
        title="Communities in the network"
        description="Select a community in the legend or below to highlight its members."
        scope={{ kind: 'top' }}
        youId={session.user.id}
        highlight={highlight}
        onHighlightChange={setHighlight}
      />
      <section className={styles.section} aria-labelledby="all-communities">
        <div className={styles.sectionHeader}>
          <h2 id="all-communities" className={styles.sectionTitle}>
            All communities
          </h2>
          {items.length > 0 ? (
            <SelectField
              label="Highlight in graph"
              fieldClassName={styles.highlightField}
              value={highlight === null ? '' : String(highlight)}
              onChange={(event) => {
                setHighlight(parseCommunityId(event.target.value));
              }}
            >
              <option value="">None</option>
              {items.map((community) => (
                <option key={community.id} value={community.id}>
                  {communityLabel(community.id)} ({formatInteger(community.size)} people)
                </option>
              ))}
            </SelectField>
          ) : null}
        </div>
        {list}
      </section>
    </div>
  );
}

interface CommunityCardProps {
  community: Community;
  slot: PaletteSlot;
  share: number;
  youId: number;
  isMine: boolean;
  highlighted: boolean;
  onShowInGraph: () => void;
}

function CommunityCard({
  community,
  slot,
  share,
  youId,
  isMine,
  highlighted,
  onShowInGraph,
}: CommunityCardProps) {
  const headingId = `community-${String(community.id)}`;
  const members = community.members.slice(0, MEMBERS_SHOWN);
  const unlisted = community.size - members.length;
  return (
    <li>
      <article
        className={`${styles.card} ${highlighted ? styles.highlighted : ''}`}
        aria-labelledby={headingId}
      >
        <header className={styles.cardHeader}>
          <span className="community-swatch" data-slot={slot} />
          <h3 id={headingId} className={styles.cardTitle}>
            {communityLabel(community.id)}
          </h3>
          {isMine ? <Badge tone="accent">Your community</Badge> : null}
        </header>
        <p className={styles.size}>
          <strong>{formatInteger(community.size)}</strong>{' '}
          {community.size === 1 ? 'person' : 'people'} ·{' '}
          {formatPercent(share, share < 0.01 ? 2 : 1)} of the network
        </p>
        <p className={styles.dominant}>
          Mostly <strong>{community.dominant_branch}</strong> in {community.dominant_category}
        </p>
        {members.length > 0 ? (
          <>
            <p className={styles.membersLabel}>Most influential members</p>
            <ul role="list" className={styles.members}>
              {members.map((member) => (
                <li key={member.id}>
                  <UserChip user={member} variant="pill" isYou={member.id === youId} />
                </li>
              ))}
            </ul>
            {unlisted > 0 ? (
              <p className={styles.more}>and {formatInteger(unlisted)} more</p>
            ) : null}
          </>
        ) : null}
        <div className={styles.cardActions}>
          <Button size="sm" icon="eye" aria-pressed={highlighted} onClick={onShowInGraph}>
            {highlighted ? 'Highlighted in graph' : 'Highlight in graph'}
          </Button>
        </div>
      </article>
    </li>
  );
}
