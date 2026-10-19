import { Link, useParams } from 'react-router';
import { hasStatus } from '../../api/errors';
import { useQuestion } from '../../api/queries';
import type { Answer } from '../../api/types';
import { Badge } from '../../components/Badge';
import { ButtonLink } from '../../components/Button';
import { Card } from '../../components/Card';
import { Icon } from '../../components/Icon';
import { PageHeader } from '../../components/PageHeader';
import { PageSkeleton } from '../../components/PageSkeleton';
import { StatGrid, StatTile } from '../../components/StatTile';
import { EmptyState, ErrorState } from '../../components/StatusViews';
import { TagChip } from '../../components/TagChip';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { formatDate, formatInteger } from '../../lib/format';
import { normalizeTags, safeExternalUrl } from '../../lib/text';
import { NotFoundPage } from '../errors/NotFoundPage';
import styles from './TopicsPage.module.css';

function parseId(value: string | undefined): number | null {
  return value !== undefined && /^[1-9]\d{0,15}$/.test(value) && Number.isSafeInteger(Number(value))
    ? Number(value)
    : null;
}

export function QuestionRoute() {
  const id = parseId(useParams().questionId);
  if (id === null) return <NotFoundPage title="Question not found" />;
  return <QuestionPage key={id} id={id} />;
}

export function QuestionPage({ id }: { id: number }) {
  const question = useQuestion(id);
  useDocumentTitle(question.data?.title ?? 'Question');

  if (question.isPending) return <PageSkeleton label="Loading question…" />;
  if (question.isError) {
    if (hasStatus(question.error, 404)) {
      return (
        <NotFoundPage
          title="Question not found"
          message="This question is not part of the 2008 sample."
        />
      );
    }
    return (
      <ErrorState
        error={question.error}
        title="We couldn't load this question"
        onRetry={() => void question.refetch()}
        retrying={question.isFetching}
      />
    );
  }

  const data = question.data;
  const tags = normalizeTags(data.tags);
  const url = safeExternalUrl(data.url);
  return (
    <div className={styles.page}>
      <PageHeader
        before={
          <ButtonLink to="/topics" variant="ghost" size="sm" icon="arrowLeft">
            All topics
          </ButtonLink>
        }
        title={data.title}
        meta={
          <>
            Asked <time dateTime={data.created_at}>{formatDate(data.created_at)}</time>
            {data.owner ? (
              <>
                {' '}
                by <Link to={`/users/${String(data.owner.id)}`}>{data.owner.display_name}</Link>
              </>
            ) : null}
            {data.closed_at ? (
              <>
                {' '}
                · <Badge tone="warning">Closed {formatDate(data.closed_at)}</Badge>
              </>
            ) : null}
          </>
        }
        actions={
          url ? (
            <a href={url} target="_blank" rel="noopener noreferrer" className={styles.external}>
              Read on Stack Overflow <Icon name="external" size={14} />{' '}
              <span className="visually-hidden">(opens in a new tab)</span>
            </a>
          ) : undefined
        }
      />
      {tags.length > 0 ? (
        <ul role="list" className={styles.questionTags} aria-label="Tags">
          {tags.map((name) => (
            <li key={name}>
              <TagChip tag={name} />
            </li>
          ))}
        </ul>
      ) : null}
      <StatGrid label="Question statistics">
        <StatTile label="Score" value={formatInteger(data.score)} />
        <StatTile label="Answers" value={formatInteger(data.answer_count)} />
        <StatTile label="Views" value={formatInteger(data.view_count)} />
        <StatTile label="Comments" value={formatInteger(data.comment_count)} />
      </StatGrid>
      <Card
        title="Answers"
        description="The dataset holds who answered and how it was received; the answer text is on Stack Overflow."
      >
        {data.answers.length === 0 ? (
          <EmptyState compact title="No answers in the sample" icon="topics" />
        ) : (
          <ol className={styles.answers}>
            {data.answers.map((answer) => (
              <AnswerItem key={answer.id} answer={answer} />
            ))}
          </ol>
        )}
      </Card>
    </div>
  );
}

function AnswerItem({ answer }: { answer: Answer }) {
  const url = safeExternalUrl(answer.url);
  return (
    <li className={styles.answer}>
      <div className={styles.counts}>
        <span className={styles.count}>
          <strong>{formatInteger(answer.score)}</strong> score
        </span>
      </div>
      <div className={styles.questionBody}>
        <p className={styles.answerOwner}>
          {answer.owner ? (
            <Link to={`/users/${String(answer.owner.id)}`}>{answer.owner.display_name}</Link>
          ) : (
            'Unknown author'
          )}
          {answer.is_accepted ? (
            <Badge tone="success">
              <Icon name="check" size={12} /> Accepted
            </Badge>
          ) : null}
        </p>
        <p className={styles.meta}>
          Answered <time dateTime={answer.created_at}>{formatDate(answer.created_at)}</time>
          {url ? (
            <>
              {' '}
              ·{' '}
              <a href={url} target="_blank" rel="noopener noreferrer">
                View answer{' '}
                <span className="visually-hidden">on Stack Overflow (opens in a new tab)</span>
              </a>
            </>
          ) : null}
        </p>
      </div>
    </li>
  );
}
