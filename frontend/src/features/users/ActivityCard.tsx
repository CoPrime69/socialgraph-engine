import { useUserActivity } from '../../api/queries';
import type { BadgeClass, ProfileSource, TopPost, UserActivity } from '../../api/types';
import { Badge } from '../../components/Badge';
import { Card } from '../../components/Card';
import { Icon } from '../../components/Icon';
import { Skeleton } from '../../components/Skeleton';
import { StatGrid, StatTile } from '../../components/StatTile';
import { EmptyState, ErrorState, LoadingState } from '../../components/StatusViews';
import { TagChip } from '../../components/TagChip';
import { formatDate, formatInteger, pluralize } from '../../lib/format';
import { normalizeTags, safeExternalUrl } from '../../lib/text';
import styles from './users.module.css';

const BADGE_CLASS: Record<BadgeClass, string> = { 1: 'Gold', 2: 'Silver', 3: 'Bronze' };

function isEmpty(activity: UserActivity): boolean {
  const { totals, badges } = activity;
  return (
    totals.questions + totals.answers + totals.comments === 0 &&
    badges.gold + badges.silver + badges.bronze === 0 &&
    activity.top_tags.length === 0 &&
    activity.top_posts.length === 0
  );
}

interface ActivityCardProps {
  userId: number;
  name: string;
  source: ProfileSource;
}

/** Stack Overflow activity from content-service: totals, top tags, badges and top posts. */
export function ActivityCard({ userId, name, source }: ActivityCardProps) {
  const activity = useUserActivity(userId);
  const data = activity.data;

  let body;
  if (activity.isPending) {
    body = (
      <LoadingState label="Loading Stack Overflow activity…">
        <div className={styles.skeletonRows}>
          <Skeleton height="5rem" radius="10px" />
          <Skeleton height="1.75rem" width="70%" />
          <Skeleton height="3rem" />
          <Skeleton height="3rem" />
        </div>
      </LoadingState>
    );
  } else if (!data) {
    body = (
      <ErrorState
        compact
        error={activity.error}
        title="Activity is unavailable"
        onRetry={() => void activity.refetch()}
        retrying={activity.isFetching}
      />
    );
  } else if (isEmpty(data)) {
    body = (
      <EmptyState compact title="No Stack Overflow activity" icon="topics">
        {source === 'member'
          ? `${name} joined SocialGraph directly, so there are no imported posts or badges.`
          : `${name} has no posts, comments or badges in the 2008 sample.`}
      </EmptyState>
    );
  } else {
    body = <ActivityDetails activity={data} />;
  }

  return (
    <Card
      title="Stack Overflow activity"
      description="Questions, answers, comments and badges from August to December 2008."
    >
      {body}
    </Card>
  );
}

function ActivityDetails({ activity }: { activity: UserActivity }) {
  const { totals, badges } = activity;
  const badgeCount = badges.gold + badges.silver + badges.bronze;
  return (
    <div className={styles.activity}>
      <StatGrid label="Activity totals">
        <StatTile
          label="Answers"
          value={formatInteger(totals.answers)}
          detail={`${formatInteger(totals.accepted_answers)} accepted · score ${formatInteger(totals.answer_score)}`}
        />
        <StatTile
          label="Questions"
          value={formatInteger(totals.questions)}
          detail={`Score ${formatInteger(totals.question_score)}`}
        />
        <StatTile label="Comments" value={formatInteger(totals.comments)} />
        <StatTile
          label="Badges"
          value={formatInteger(badgeCount)}
          detail={`${formatInteger(badges.gold)} gold · ${formatInteger(badges.silver)} silver · ${formatInteger(badges.bronze)} bronze`}
        />
      </StatGrid>

      {activity.top_tags.length > 0 ? (
        <section aria-labelledby={`tags-${String(activity.user_id)}`} className={styles.subsection}>
          <h3 id={`tags-${String(activity.user_id)}`} className={styles.subheading}>
            Top tags
          </h3>
          <ul role="list" className={styles.tagList}>
            {activity.top_tags.map((tag) => (
              <li key={tag.tag}>
                <TagChip tag={tag.tag} count={tag.posts} countLabel="posts" />
              </li>
            ))}
          </ul>
        </section>
      ) : null}

      {activity.top_posts.length > 0 ? (
        <section
          aria-labelledby={`posts-${String(activity.user_id)}`}
          className={styles.subsection}
        >
          <h3 id={`posts-${String(activity.user_id)}`} className={styles.subheading}>
            Top posts
          </h3>
          <ol className={styles.posts}>
            {activity.top_posts.map((post) => (
              <PostItem key={`${post.type}-${String(post.id)}`} post={post} />
            ))}
          </ol>
        </section>
      ) : null}

      {badges.recent.length > 0 ? (
        <section
          aria-labelledby={`badges-${String(activity.user_id)}`}
          className={styles.subsection}
        >
          <h3 id={`badges-${String(activity.user_id)}`} className={styles.subheading}>
            Recent badges
          </h3>
          <ul role="list" className={styles.badges}>
            {badges.recent.map((badge) => (
              <li key={`${badge.name}-${badge.awarded_at}`} className={styles.badge}>
                <span className={styles.medal} data-class={badge.class} aria-hidden="true" />
                <span className={styles.badgeName}>{badge.name}</span>
                <span className={styles.badgeMeta}>
                  {BADGE_CLASS[badge.class]} ·{' '}
                  <time dateTime={badge.awarded_at}>{formatDate(badge.awarded_at)}</time>
                </span>
              </li>
            ))}
          </ul>
        </section>
      ) : null}
    </div>
  );
}

function PostItem({ post }: { post: TopPost }) {
  const url = safeExternalUrl(post.url);
  const tags = normalizeTags(post.tags);
  return (
    <li className={styles.post}>
      <div className={styles.postScore}>
        <span className={styles.postScoreValue}>{formatInteger(post.score)}</span>
        <span className={styles.postScoreLabel}>score</span>
      </div>
      <div className={styles.postBody}>
        <p className={styles.postTitle}>
          <Badge tone={post.type === 'question' ? 'accent' : 'neutral'}>
            {post.type === 'question' ? 'Question' : 'Answer'}
          </Badge>{' '}
          {url ? (
            <a href={url} target="_blank" rel="noopener noreferrer">
              {post.title}
              <Icon name="external" size={12} className={styles.inlineIcon} />{' '}
              <span className="visually-hidden">(opens Stack Overflow in a new tab)</span>
            </a>
          ) : (
            post.title
          )}
        </p>
        <p className={styles.postMeta}>
          <time dateTime={post.created_at}>{formatDate(post.created_at)}</time>
          {typeof post.view_count === 'number' ? ` · ${pluralize(post.view_count, 'view')}` : ''}
        </p>
        {tags.length > 0 ? (
          <ul role="list" className={styles.postTags} aria-label="Tags">
            {tags.map((tag) => (
              <li key={tag}>
                <TagChip tag={tag} size="sm" />
              </li>
            ))}
          </ul>
        ) : null}
      </div>
    </li>
  );
}
