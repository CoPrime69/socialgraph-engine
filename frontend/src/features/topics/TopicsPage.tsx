import { useState } from 'react';
import { Link, useSearchParams } from 'react-router';
import { useQuestions, useTags } from '../../api/queries';
import type { QuestionSort, QuestionSummary } from '../../api/types';
import { Card } from '../../components/Card';
import { TextField } from '../../components/Field';
import { Icon } from '../../components/Icon';
import { PageHeader } from '../../components/PageHeader';
import { Pagination } from '../../components/Pagination';
import { SegmentedControl } from '../../components/SegmentedControl';
import { Skeleton } from '../../components/Skeleton';
import { EmptyState, ErrorState, LoadingState } from '../../components/StatusViews';
import { TagChip } from '../../components/TagChip';
import { useDebouncedCallback } from '../../hooks/useDebouncedCallback';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { formatDate, formatInteger, pluralize } from '../../lib/format';
import { normalizeTags } from '../../lib/text';
import styles from './TopicsPage.module.css';

const QUESTIONS_PAGE_SIZE = 20;
const TAG_LIMIT = 40;
const DEBOUNCE_MS = 300;
const SORTS: readonly { value: QuestionSort; label: string }[] = [
  { value: 'score', label: 'Top scored' },
  { value: 'recent', label: 'Newest' },
];

function parsePage(value: string | null): number {
  return value !== null && /^\d{1,6}$/.test(value) ? Math.max(1, Number(value)) : 1;
}

/** Text box whose value is copied to a URL parameter after a pause in typing. */
function useDebouncedParam(value: string, commit: (next: string) => void) {
  const [draft, setDraft] = useState(value);
  const [synced, setSynced] = useState(value);
  const [pushed, setPushed] = useState<string | null>(null);
  if (value !== synced) {
    setSynced(value);
    if (value !== pushed) setDraft(value);
  }
  const debounced = useDebouncedCallback((next: string) => {
    setPushed(next.trim());
    commit(next.trim());
  }, DEBOUNCE_MS);
  return {
    draft,
    onChange: (next: string) => {
      setDraft(next);
      debounced.debounced(next);
    },
  };
}

export function TopicsPage() {
  const [searchParams, setSearchParams] = useSearchParams();
  const tag = searchParams.get('tag')?.trim() ?? '';
  const q = searchParams.get('q') ?? '';
  const sort: QuestionSort = searchParams.get('sort') === 'recent' ? 'recent' : 'score';
  const page = parsePage(searchParams.get('page'));
  const [tagFilter, setTagFilter] = useState('');
  const [tagQuery, setTagQuery] = useState('');
  const tagSearch = useDebouncedCallback((value: string) => {
    setTagQuery(value.trim());
  }, DEBOUNCE_MS);
  useDocumentTitle(tag ? `Questions tagged ${tag}` : 'Topics');

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
  const questionSearch = useDebouncedParam(q, (next) => {
    update({ q: next });
  });

  const tags = useTags({ q: tagQuery || undefined, limit: TAG_LIMIT });
  const questions = useQuestions({
    tag: tag || undefined,
    q: q || undefined,
    sort,
    limit: QUESTIONS_PAGE_SIZE,
    offset: (page - 1) * QUESTIONS_PAGE_SIZE,
  });

  const tagLink = (name: string | null) => {
    const next = new URLSearchParams();
    if (name) next.set('tag', name);
    if (sort !== 'score') next.set('sort', sort);
    const search = next.toString();
    return { pathname: '/topics', search: search ? `?${search}` : '' };
  };

  let tagList;
  if (tags.isPending) {
    tagList = (
      <LoadingState label="Loading tags…">
        <div className={styles.skeletonList}>
          {Array.from({ length: 10 }, (_, i) => (
            <Skeleton key={i} height="1.5rem" />
          ))}
        </div>
      </LoadingState>
    );
  } else if (!tags.data) {
    tagList = (
      <ErrorState
        compact
        error={tags.error}
        onRetry={() => void tags.refetch()}
        retrying={tags.isFetching}
      />
    );
  } else if (tags.data.items.length === 0) {
    tagList = <EmptyState compact title="No tags match" icon="search" />;
  } else {
    tagList = (
      <ul role="list" className={styles.tags}>
        {tags.data.items.map((item) => (
          <li key={item.name}>
            <Link
              to={tagLink(item.name)}
              className={styles.tagLink}
              aria-current={item.name === tag ? 'true' : undefined}
            >
              <span className={styles.tagName}>{item.name}</span>{' '}
              <span className={styles.tagCount}>
                {formatInteger(item.question_count)}{' '}
                <span className="visually-hidden">questions</span>
              </span>
            </Link>
          </li>
        ))}
      </ul>
    );
  }

  const data = questions.data;
  let list;
  if (questions.isPending) {
    list = (
      <LoadingState label="Loading questions…">
        <div className={styles.skeletonList}>
          {Array.from({ length: 6 }, (_, i) => (
            <Skeleton key={i} height="4.5rem" radius="4px" />
          ))}
        </div>
      </LoadingState>
    );
  } else if (!data) {
    list = (
      <ErrorState
        error={questions.error}
        onRetry={() => void questions.refetch()}
        retrying={questions.isFetching}
      />
    );
  } else if (data.items.length === 0) {
    list = (
      <EmptyState title="No questions found" icon="topics">
        {q
          ? 'Try different words or another tag.'
          : 'There are no questions with this tag in the sample.'}
      </EmptyState>
    );
  } else {
    list = (
      <>
        <ol className={styles.questions}>
          {data.items.map((question) => (
            <QuestionItem key={question.id} question={question} />
          ))}
        </ol>
        <Pagination
          page={page}
          pageSize={QUESTIONS_PAGE_SIZE}
          total={data.total}
          noun="questions"
          disabled={questions.isPlaceholderData}
          onPageChange={(next) => {
            update({ page: next > 1 ? String(next) : null }, false);
            window.scrollTo({ top: 0 });
          }}
        />
      </>
    );
  }

  return (
    <div className={styles.page}>
      <PageHeader
        title="Topics"
        description="What the community asked about in late 2008: browse questions by tag."
      />
      <div className={styles.columns}>
        <Card title="Tags" headingLevel={2} className={styles.tagCard}>
          <div className={styles.tagPanel}>
            <TextField
              label="Find a tag"
              type="search"
              placeholder="e.g. python"
              autoComplete="off"
              spellCheck={false}
              value={tagFilter}
              onChange={(event) => {
                setTagFilter(event.target.value);
                tagSearch.debounced(event.target.value);
              }}
            />
            {tag ? (
              <Link to={tagLink(null)} className={styles.allLink}>
                <Icon name="close" size={14} /> Clear tag: {tag}
              </Link>
            ) : null}
            {tagList}
          </div>
        </Card>
        <Card
          title={tag ? `Questions tagged “${tag}”` : 'All questions'}
          description={data ? pluralize(data.total, 'question') : undefined}
          busy={questions.isPlaceholderData && questions.isFetching}
          toolbar={
            <div className={styles.questionTools}>
              <TextField
                label="Search titles"
                type="search"
                autoComplete="off"
                value={questionSearch.draft}
                onChange={(event) => {
                  questionSearch.onChange(event.target.value);
                }}
                fieldClassName={styles.questionSearch}
              />
              <SegmentedControl
                label="Sort questions"
                size="sm"
                options={SORTS}
                value={sort}
                onChange={(value) => {
                  update({ sort: value === 'score' ? null : value });
                }}
              />
            </div>
          }
        >
          {list}
        </Card>
      </div>
    </div>
  );
}

function QuestionItem({ question }: { question: QuestionSummary }) {
  const tags = normalizeTags(question.tags);
  return (
    <li className={styles.question}>
      <div className={styles.counts}>
        <span className={styles.count}>
          <strong>{formatInteger(question.score)}</strong> score
        </span>
        <span className={styles.count}>
          <strong>{formatInteger(question.answer_count)}</strong>{' '}
          {question.answer_count === 1 ? 'answer' : 'answers'}
        </span>
      </div>
      <div className={styles.questionBody}>
        <Link to={`/questions/${String(question.id)}`} className={styles.questionTitle}>
          {question.title}
        </Link>
        {tags.length > 0 ? (
          <ul role="list" className={styles.questionTags} aria-label="Tags">
            {tags.map((name) => (
              <li key={name}>
                <TagChip tag={name} size="sm" />
              </li>
            ))}
          </ul>
        ) : null}
        <p className={styles.meta}>
          Asked <time dateTime={question.created_at}>{formatDate(question.created_at)}</time>
          {question.owner ? (
            <>
              {' '}
              by{' '}
              <Link to={`/users/${String(question.owner.id)}`}>{question.owner.display_name}</Link>
            </>
          ) : null}{' '}
          · {pluralize(question.view_count, 'view')}
        </p>
      </div>
    </li>
  );
}
