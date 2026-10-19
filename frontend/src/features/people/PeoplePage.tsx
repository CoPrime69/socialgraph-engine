import { useId, useRef, useState, type FormEvent } from 'react';
import { useSearchParams } from 'react-router';
import { useTaxonomy, useUserList } from '../../api/queries';
import type { UserListParams, UserSort } from '../../api/types';
import { useSession } from '../../auth/authContext';
import { BranchOptions } from '../../components/BranchOptions';
import { Button } from '../../components/Button';
import { Card } from '../../components/Card';
import table from '../../components/DataTable.module.css';
import { SelectField, TextField } from '../../components/Field';
import { Icon } from '../../components/Icon';
import { PageHeader } from '../../components/PageHeader';
import { Pagination } from '../../components/Pagination';
import { Skeleton } from '../../components/Skeleton';
import { EmptyState, ErrorState, LoadingState } from '../../components/StatusViews';
import { UserChip } from '../../components/UserChip';
import { useDebouncedCallback } from '../../hooks/useDebouncedCallback';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { formatInteger, formatPercentile } from '../../lib/format';
import styles from './PeoplePage.module.css';

export const PEOPLE_PAGE_SIZE = 20;
export const SEARCH_DEBOUNCE_MS = 300;

const SORTS: readonly { value: UserSort; label: string }[] = [
  { value: 'reputation', label: 'Reputation (highest first)' },
  { value: 'name', label: 'Name (A–Z)' },
  { value: 'id', label: 'Account id (oldest first)' },
];

const MIN_REPUTATION = [0, 100, 1000, 10_000, 50_000] as const;

function parseSort(value: string | null): UserSort {
  return value === 'name' || value === 'id' ? value : 'reputation';
}

function parsePositiveInt(value: string | null, fallback: number): number {
  if (value === null || !/^\d{1,9}$/.test(value)) return fallback;
  return Number(value);
}

export function PeoplePage() {
  useDocumentTitle('People');
  const session = useSession();
  const taxonomy = useTaxonomy();
  const [searchParams, setSearchParams] = useSearchParams();
  const resultsRef = useRef<HTMLDivElement>(null);
  const statusId = useId();

  const q = searchParams.get('q') ?? '';
  const category = searchParams.get('category') ?? '';
  const branch = searchParams.get('branch') ?? '';
  const sort = parseSort(searchParams.get('sort'));
  const minReputation = parsePositiveInt(searchParams.get('min_reputation'), 0);
  const page = Math.max(1, parsePositiveInt(searchParams.get('page'), 1));

  // The search box is edited locally and copied to the URL after a pause in typing; when the
  // URL changes for another reason (back button, "Clear filters") the box follows it.
  const [term, setTerm] = useState(q);
  const [syncedQ, setSyncedQ] = useState(q);
  const [pushedQ, setPushedQ] = useState<string | null>(null);
  if (q !== syncedQ) {
    setSyncedQ(q);
    // Our own debounced update must not overwrite what was typed since.
    if (q !== pushedQ) setTerm(q);
  }

  const update = (changes: Record<string, string | null>, resetPage = true) => {
    setSearchParams(
      (previous) => {
        const next = new URLSearchParams(previous);
        for (const [key, value] of Object.entries(changes)) {
          if (value === null || value === '') next.delete(key);
          else next.set(key, value);
        }
        if (resetPage) next.delete('page');
        return next;
      },
      { replace: true, preventScrollReset: true },
    );
  };

  const search = useDebouncedCallback((value: string) => {
    setPushedQ(value.trim());
    update({ q: value.trim() });
  }, SEARCH_DEBOUNCE_MS);

  const params: UserListParams = {
    q: q || undefined,
    category: category || undefined,
    branch: branch || undefined,
    sort,
    min_reputation: minReputation > 0 ? minReputation : undefined,
    limit: PEOPLE_PAGE_SIZE,
    offset: (page - 1) * PEOPLE_PAGE_SIZE,
  };
  const people = useUserList(params);
  const data = people.data;
  const filtered = q !== '' || category !== '' || branch !== '' || minReputation > 0;

  const clearFilters = () => {
    search.cancel();
    setTerm('');
    setPushedQ('');
    setSearchParams(sort === 'reputation' ? {} : { sort }, { replace: true });
  };

  const onSubmit = (event: FormEvent) => {
    event.preventDefault();
    search.cancel();
    setPushedQ(term.trim());
    update({ q: term.trim() });
  };

  let results;
  if (people.isPending) {
    results = (
      <LoadingState label="Loading people…">
        <div className={styles.skeletonRows}>
          {Array.from({ length: 8 }, (_, i) => (
            <Skeleton key={i} height="2.75rem" />
          ))}
        </div>
      </LoadingState>
    );
  } else if (people.isError && !data) {
    results = (
      <ErrorState
        error={people.error}
        onRetry={() => void people.refetch()}
        retrying={people.isFetching}
      />
    );
  } else if (!data || data.items.length === 0) {
    results = (
      <EmptyState
        title={filtered ? 'No people match these filters' : 'No people yet'}
        icon="search"
        action={
          filtered ? (
            <Button size="sm" onClick={clearFilters}>
              Clear filters
            </Button>
          ) : undefined
        }
      >
        {filtered ? 'Try a shorter search or fewer filters.' : undefined}
      </EmptyState>
    );
  } else {
    results = (
      <>
        <table role="table" className={`${table.table} ${table.stack}`}>
          <caption className="visually-hidden">People, sorted by {sortLabel(sort)}</caption>
          <thead role="rowgroup">
            <tr role="row">
              <th role="columnheader" scope="col">
                Person
              </th>
              <th role="columnheader" scope="col">
                Technology
              </th>
              <th role="columnheader" scope="col">
                Location
              </th>
              <th role="columnheader" scope="col" className={table.numeric}>
                Reputation
              </th>
              <th role="columnheader" scope="col" className={table.numeric}>
                Connections
              </th>
            </tr>
          </thead>
          <tbody role="rowgroup">
            {data.items.map((person) => (
              <tr role="row" key={person.id}>
                <td role="cell" data-primary="">
                  <UserChip user={person} isYou={person.id === session.user.id} />
                </td>
                <td role="cell" data-label="Technology">
                  <span className={styles.branch}>{person.branch}</span>
                  <span className={styles.category}>{person.category}</span>
                </td>
                <td role="cell" data-label="Location" className={styles.location}>
                  {person.location ?? <span className={table.muted}>—</span>}
                </td>
                <td role="cell" data-label="Reputation" className={table.numeric}>
                  <span className={styles.reputation}>{formatInteger(person.reputation)}</span>
                  <span className={styles.percentile}>{formatPercentile(person.influence)}</span>
                </td>
                <td role="cell" data-label="Connections" className={table.numeric}>
                  {formatInteger(person.connection_count)}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
        <div className={styles.pagination}>
          <Pagination
            page={page}
            pageSize={PEOPLE_PAGE_SIZE}
            total={data.total}
            noun="people"
            disabled={people.isPlaceholderData}
            onPageChange={(next) => {
              update({ page: next > 1 ? String(next) : null }, false);
              resultsRef.current?.scrollIntoView({ block: 'start' });
            }}
          />
        </div>
      </>
    );
  }

  return (
    <div className={styles.page}>
      <PageHeader
        title="People"
        description="Find anyone in the network by name or handle, technology or reputation."
      />
      <Card
        toolbar={
          <form className={styles.filters} role="search" onSubmit={onSubmit}>
            <TextField
              label="Search"
              type="search"
              placeholder="Name or handle"
              autoComplete="off"
              spellCheck={false}
              value={term}
              fieldClassName={styles.searchField}
              onChange={(event) => {
                setTerm(event.target.value);
                search.debounced(event.target.value);
              }}
              trailing={<Icon name="search" size={16} />}
            />
            <SelectField
              label="Category"
              value={category}
              disabled={!taxonomy.data}
              onChange={(event) => {
                const next = event.target.value;
                const keepBranch =
                  branch !== '' && taxonomy.data?.branch_categories[branch] === next;
                update({ category: next, branch: keepBranch ? branch : null });
              }}
            >
              <option value="">All categories</option>
              {taxonomy.data?.categories.map((name) => (
                <option key={name} value={name}>
                  {name}
                </option>
              ))}
            </SelectField>
            <SelectField
              label="Technology"
              value={branch}
              disabled={!taxonomy.data}
              onChange={(event) => {
                const next = event.target.value;
                const owner = next === '' ? undefined : taxonomy.data?.branch_categories[next];
                update({ branch: next, category: owner ?? (next === '' ? category : null) });
              }}
            >
              <option value="">All technologies</option>
              {taxonomy.data ? (
                <BranchOptions taxonomy={taxonomy.data} category={category} />
              ) : null}
            </SelectField>
            <SelectField
              label="Reputation"
              value={String(minReputation)}
              onChange={(event) => {
                update({ min_reputation: event.target.value === '0' ? null : event.target.value });
              }}
            >
              {MIN_REPUTATION.map((value) => (
                <option key={value} value={value}>
                  {value === 0 ? 'Any reputation' : `${formatInteger(value)} or more`}
                </option>
              ))}
            </SelectField>
            <SelectField
              label="Sort by"
              value={sort}
              onChange={(event) => {
                update({ sort: event.target.value === 'reputation' ? null : event.target.value });
              }}
            >
              {SORTS.map((option) => (
                <option key={option.value} value={option.value}>
                  {option.label}
                </option>
              ))}
            </SelectField>
            {filtered ? (
              <Button variant="ghost" size="sm" className={styles.clear} onClick={clearFilters}>
                Clear filters
              </Button>
            ) : null}
          </form>
        }
        flush={data !== undefined && data.items.length > 0}
        busy={people.isPlaceholderData && people.isFetching}
      >
        <div ref={resultsRef} className={styles.results}>
          <p id={statusId} role="status" className="visually-hidden">
            {data
              ? `${formatInteger(data.total)} ${data.total === 1 ? 'person' : 'people'} found`
              : ''}
          </p>
          {results}
        </div>
      </Card>
    </div>
  );
}

function sortLabel(sort: UserSort): string {
  return SORTS.find((option) => option.value === sort)?.label.toLowerCase() ?? sort;
}
