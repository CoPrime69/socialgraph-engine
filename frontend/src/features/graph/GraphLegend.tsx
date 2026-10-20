import { useId, useMemo } from 'react';
import type { GraphNode } from '../../api/types';
import { Button } from '../../components/Button';
import { formatInteger, pluralize } from '../../lib/format';
import { summarizeLegend } from './legend';
import { communityLabel, type PaletteSlot } from './palette';
import styles from './GraphCard.module.css';

interface GraphLegendProps {
  nodes: readonly GraphNode[];
  slots: ReadonlyMap<number, PaletteSlot>;
  highlight: number | null;
  onHighlight: (community: number | null) => void;
  /** Whether the signed-in user is among the drawn nodes. */
  youInView: boolean;
}

export function GraphLegend({ nodes, slots, highlight, onHighlight, youInView }: GraphLegendProps) {
  const titleId = useId();
  const { entries, otherCommunities, otherPeople } = useMemo(
    () => summarizeLegend(nodes, slots),
    [nodes, slots],
  );
  const highlightInView = highlight === null || nodes.some((node) => node.community === highlight);

  return (
    <div className={styles.legend}>
      <p id={titleId} className={styles.legendTitle}>
        Communities <span className={styles.legendNote}>· select one to highlight it</span>
      </p>
      <ul role="list" aria-labelledby={titleId} className={styles.legendList}>
        {entries.map((entry) => (
          <li key={entry.id}>
            <button
              type="button"
              className={styles.legendItem}
              aria-pressed={highlight === entry.id}
              onClick={() => {
                onHighlight(highlight === entry.id ? null : entry.id);
              }}
            >
              <span className="community-swatch" data-slot={entry.slot} />
              <span>{communityLabel(entry.id)}</span>{' '}
              <span className={styles.legendCount}>
                {formatInteger(entry.count)} <span className="visually-hidden">shown</span>
              </span>
            </button>
          </li>
        ))}
        {otherPeople > 0 ? (
          <li className={styles.legendStatic}>
            <span className="community-swatch" data-slot={0} />
            <span>
              {pluralize(otherCommunities, 'smaller community', 'smaller communities')}
            </span>{' '}
            <span className={styles.legendCount}>
              {formatInteger(otherPeople)} <span className="visually-hidden">shown</span>
            </span>
          </li>
        ) : null}
      </ul>
      {highlight !== null && !highlightInView ? (
        <p className={styles.legendWarning} role="status">
          {communityLabel(highlight)} has no members among the people shown.{' '}
          <Button
            size="sm"
            variant="ghost"
            onClick={() => {
              onHighlight(null);
            }}
          >
            Clear highlight
          </Button>
        </p>
      ) : null}
      <p className={styles.key}>
        <span className={styles.sizeKey} aria-hidden="true">
          <span />
          <span />
          <span />
        </span>
        Circle size shows PageRank.
        {youInView ? (
          <>
            <span className={styles.youKey} aria-hidden="true" /> The ringed circle is you.
          </>
        ) : null}
      </p>
    </div>
  );
}
