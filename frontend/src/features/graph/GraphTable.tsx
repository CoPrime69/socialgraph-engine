import { useMemo, useState } from 'react';
import type { GraphNode } from '../../api/types';
import { Pagination } from '../../components/Pagination';
import { UserChip } from '../../components/UserChip';
import table from '../../components/DataTable.module.css';
import { formatInteger, formatSignificant } from '../../lib/format';
import { communityLabel, type PaletteSlot } from './palette';

const PAGE_SIZE = 20;

interface GraphTableProps {
  nodes: readonly GraphNode[];
  slots: ReadonlyMap<number, PaletteSlot>;
  youId: number | null;
  /** When set, only that community's members are listed (mirrors the graph highlight). */
  highlight: number | null;
  caption: string;
}

/** The graph's people as a sortable-by-PageRank table: the accessible view of the picture. */
export function GraphTable({ nodes, slots, youId, highlight, caption }: GraphTableProps) {
  const [requestedPage, setPage] = useState(1);
  const rows = useMemo(
    () =>
      nodes
        .filter((node) => highlight === null || node.community === highlight)
        .sort((a, b) => b.pagerank - a.pagerank || a.id - b.id),
    [nodes, highlight],
  );
  const pageCount = Math.max(1, Math.ceil(rows.length / PAGE_SIZE));
  const page = Math.min(requestedPage, pageCount);
  const visible = rows.slice((page - 1) * PAGE_SIZE, page * PAGE_SIZE);

  return (
    <div>
      <table role="table" className={`${table.table} ${table.stack}`}>
        <caption className="visually-hidden">{caption}</caption>
        <thead role="rowgroup">
          <tr role="row">
            <th role="columnheader" scope="col" className={table.rank}>
              #
            </th>
            <th role="columnheader" scope="col">
              Person
            </th>
            <th role="columnheader" scope="col">
              Community
            </th>
            <th role="columnheader" scope="col" className={table.numeric}>
              Reputation
            </th>
            <th role="columnheader" scope="col" className={table.numeric}>
              Connections
            </th>
            <th role="columnheader" scope="col" className={table.numeric}>
              PageRank
            </th>
          </tr>
        </thead>
        <tbody role="rowgroup">
          {visible.map((node, i) => (
            <tr role="row" key={node.id}>
              <td role="cell" data-label="#" className={table.rank}>
                {formatInteger((page - 1) * PAGE_SIZE + i + 1)}
              </td>
              <td role="cell" data-primary="">
                <UserChip user={node} isYou={node.id === youId} size="sm" />
              </td>
              <td role="cell" data-label="Community" className={table.nowrap}>
                <span className="community-swatch" data-slot={slots.get(node.community) ?? 0} />{' '}
                {communityLabel(node.community)}
              </td>
              <td role="cell" data-label="Reputation" className={table.numeric}>
                {formatInteger(node.reputation)}
              </td>
              <td role="cell" data-label="Connections" className={table.numeric}>
                {formatInteger(node.degree)}
              </td>
              <td role="cell" data-label="PageRank" className={table.numeric}>
                {formatSignificant(node.pagerank)}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      {rows.length > PAGE_SIZE ? (
        <Pagination
          page={page}
          pageSize={PAGE_SIZE}
          total={rows.length}
          onPageChange={setPage}
          noun="people"
        />
      ) : null}
    </div>
  );
}
