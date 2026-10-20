import { useId, useMemo, useState, type ReactNode } from 'react';
import { useNavigate } from 'react-router';
import { useGraph } from '../../api/queries';
import type { GraphQuery, GraphResponse } from '../../api/types';
import { Card } from '../../components/Card';
import field from '../../components/Field.module.css';
import { SegmentedControl } from '../../components/SegmentedControl';
import { Skeleton } from '../../components/Skeleton';
import { EmptyState, ErrorState, LoadingState } from '../../components/StatusViews';
import { formatInteger, pluralize } from '../../lib/format';
import { GraphLegend } from './GraphLegend';
import { GraphTable } from './GraphTable';
import { BACKBONE_THRESHOLD, strongestPerNode } from './model';
import { NetworkGraph } from './NetworkGraph';
import { useCommunitySlots } from './useCommunitySlots';
import styles from './GraphCard.module.css';

const TOP_LIMITS = [500, 1000, 1500, 3000] as const;
const DEFAULT_TOP_LIMIT = 1500;

export type GraphScope =
  | { kind: 'top' }
  /** Ego network of `focusId`, at most `limit` people (most influential first). */
  | { kind: 'ego'; focusId: number; limit: number };

type View = 'graph' | 'table';

interface GraphCardProps {
  title: string;
  description?: ReactNode;
  scope: GraphScope;
  youId: number;
  /** Controlled highlight (e.g. kept in the URL by the Communities page). */
  highlight?: number | null;
  onHighlightChange?: (community: number | null) => void;
  /** Shown instead of the graph when an ego network has nobody but its centre. */
  emptyMessage?: ReactNode;
  size?: 'md' | 'lg';
  id?: string;
}

/** "the 12 connections among them", "the 1 connection among them", "no connections among them". */
function connectionsAmongThem(count: number): string {
  if (count === 0) return 'no connections among them';
  return `the ${formatInteger(count)} ${count === 1 ? 'connection' : 'connections'} among them`;
}

function describeScope(data: GraphResponse, scope: GraphScope, depth: 1 | 2): string {
  const among = connectionsAmongThem(data.edges.length);
  if (scope.kind === 'top') {
    return `The ${formatInteger(data.nodes.length)} most influential of ${formatInteger(data.total_nodes)} people (by PageRank) and ${among}.`;
  }
  const centre = data.nodes.find((node) => node.id === scope.focusId);
  const name = centre?.name ?? 'This person';
  const others = data.nodes.length - 1;
  if (depth === 1) {
    const degree = centre?.degree ?? others;
    const subset = others < degree ? `the ${formatInteger(others)} most influential of ` : '';
    return `${name} and ${subset}${pluralize(degree, 'direct connection')}, with ${among}.`;
  }
  return `${name}, their connections and their connections’ connections: ${pluralize(data.nodes.length, 'person', 'people')} (most influential first) and ${pluralize(data.edges.length, 'connection')}.`;
}

/**
 * Network graph with its controls: scope (top-N by PageRank or an ego network's depth), graph
 * or table view, every-edge toggle, legend with community highlighting, and every state.
 */
export function GraphCard({
  title,
  description,
  scope,
  youId,
  highlight: controlledHighlight,
  onHighlightChange,
  emptyMessage,
  size = 'lg',
  id,
}: GraphCardProps) {
  const navigate = useNavigate();
  const limitId = useId();
  const [limit, setLimit] = useState<number>(DEFAULT_TOP_LIMIT);
  const [depth, setDepth] = useState<1 | 2>(1);
  const [view, setView] = useState<View>('graph');
  const [showAllEdges, setShowAllEdges] = useState(false);
  const [localHighlight, setLocalHighlight] = useState<number | null>(null);
  const highlight = controlledHighlight !== undefined ? controlledHighlight : localHighlight;
  const setHighlight = onHighlightChange ?? setLocalHighlight;

  const query: GraphQuery =
    scope.kind === 'top' ? { limit } : { limit: scope.limit, focus: scope.focusId, depth };
  const graph = useGraph(query, scope.kind === 'ego' && scope.focusId === youId);
  const data = graph.data;
  const slots = useCommunitySlots(data?.nodes);
  const focusId = scope.kind === 'ego' ? scope.focusId : null;

  const youInView = useMemo(
    () => data?.nodes.some((node) => node.id === youId) ?? false,
    [data, youId],
  );
  const backboneActive = data !== undefined && data.edges.length > BACKBONE_THRESHOLD;

  const toolbar = (
    <div className={styles.toolbar}>
      {scope.kind === 'top' ? (
        <div className={styles.inlineField}>
          <label htmlFor={limitId}>People shown</label>
          <select
            id={limitId}
            className={`${field.input} ${field.select} ${styles.select}`}
            value={limit}
            onChange={(event) => {
              setLimit(Number(event.target.value));
            }}
          >
            {TOP_LIMITS.map((value) => (
              <option key={value} value={value}>
                Top {formatInteger(value)} by PageRank
              </option>
            ))}
          </select>
        </div>
      ) : (
        <SegmentedControl
          label="How far to look"
          size="sm"
          value={depth === 1 ? '1' : '2'}
          onChange={(value) => {
            setDepth(value === '2' ? 2 : 1);
          }}
          options={[
            { value: '1', label: 'Direct connections' },
            { value: '2', label: 'Two steps away' },
          ]}
        />
      )}
      <SegmentedControl
        label="View as"
        size="sm"
        value={view}
        onChange={setView}
        options={[
          { value: 'graph', label: 'Graph' },
          { value: 'table', label: 'Table' },
        ]}
      />
    </div>
  );

  let body: ReactNode;
  if (!data) {
    body = graph.isError ? (
      <ErrorState
        error={graph.error}
        onRetry={() => void graph.refetch()}
        retrying={graph.isFetching}
      />
    ) : (
      <LoadingState label="Loading the network graph…">
        <Skeleton height={size === 'lg' ? '28rem' : '20rem'} radius="4px" />
      </LoadingState>
    );
  } else if (data.nodes.length === 0 || (scope.kind === 'ego' && data.nodes.length <= 1)) {
    body = (
      <EmptyState title="No connections to show yet" icon="network">
        {emptyMessage ?? 'Once there are connections, they will appear here.'}
      </EmptyState>
    );
  } else {
    const summary = describeScope(data, scope, depth);
    body = (
      <div className={styles.content}>
        <p className={styles.disclosure}>
          {summary}{' '}
          {backboneActive && !showAllEdges && view === 'graph'
            ? `For readability only each person’s ${String(strongestPerNode(data.nodes.length))} strongest connections are drawn.`
            : null}
        </p>
        {view === 'graph' && backboneActive ? (
          <label className={styles.checkbox}>
            <input
              type="checkbox"
              checked={showAllEdges}
              onChange={(event) => {
                setShowAllEdges(event.target.checked);
              }}
            />
            Show every connection ({formatInteger(data.edges.length)})
          </label>
        ) : null}
        {view === 'graph' ? (
          <NetworkGraph
            data={data}
            slots={slots}
            youId={youId}
            focusId={focusId}
            highlightCommunity={highlight}
            showAllEdges={showAllEdges}
            label={`Network graph. ${summary} Circles are coloured by community and sized by PageRank. The table view lists the same people.`}
            onSelectNode={(node) => {
              void navigate(`/users/${String(node.id)}`);
            }}
            size={size}
          />
        ) : (
          <GraphTable
            nodes={data.nodes}
            slots={slots}
            youId={youId}
            highlight={highlight}
            caption={`People in the graph, by PageRank. ${summary}`}
          />
        )}
        <GraphLegend
          nodes={data.nodes}
          slots={slots}
          highlight={highlight}
          onHighlight={setHighlight}
          youInView={youInView && view === 'graph'}
        />
      </div>
    );
  }

  return (
    <Card
      id={id}
      title={title}
      description={description}
      toolbar={toolbar}
      busy={graph.isPlaceholderData && graph.isFetching}
      footer={
        data ? (
          <span>
            Graph version {formatInteger(data.graph_version)}
            {data.stale ? ' · updating with your latest changes…' : ''}
          </span>
        ) : undefined
      }
    >
      {body}
    </Card>
  );
}
