import type { ReactNode } from 'react';
import type { UserProfileWithCount } from '../../api/types';
import { Badge } from '../../components/Badge';
import { Card } from '../../components/Card';
import { Icon } from '../../components/Icon';
import { Avatar } from '../../components/UserChip';
import {
  formatDate,
  formatInteger,
  formatMonthYear,
  formatPercentile,
  formatRelativeTime,
} from '../../lib/format';
import { displayHost, htmlToText, paragraphs, safeExternalUrl } from '../../lib/text';
import { ConnectionToggle } from '../network/ConnectionToggle';
import styles from './users.module.css';

function ExternalLink({ href, children }: { href: string; children: ReactNode }) {
  return (
    <a href={href} target="_blank" rel="noopener noreferrer" className={styles.external}>
      {children}
      <Icon name="external" size={14} />{' '}
      <span className="visually-hidden">(opens in a new tab)</span>
    </a>
  );
}

export function ProfileHeader({ profile, isMe }: { profile: UserProfileWithCount; isMe: boolean }) {
  const stackOverflow = safeExternalUrl(profile.external_url);
  return (
    <header className={styles.header}>
      <Avatar name={profile.name} size="lg" highlighted={isMe} />
      <div className={styles.identity}>
        <div className={styles.nameRow}>
          <h1 className={styles.name}>{profile.name}</h1>
          {isMe ? <Badge tone="accent">You</Badge> : null}
          <Badge>
            {profile.source === 'stackoverflow' ? 'Stack Overflow user' : 'SocialGraph member'}
          </Badge>
        </div>
        <p className={styles.handle}>
          @{profile.handle} · {profile.branch}, {profile.category}
        </p>
        <p className={styles.reputation}>
          <span className={styles.reputationValue}>{formatInteger(profile.reputation)}</span>{' '}
          reputation
          <span className={styles.percentile}>{formatPercentile(profile.influence)}</span>
        </p>
      </div>
      <div className={styles.headerActions}>
        {isMe ? null : <ConnectionToggle target={{ id: profile.id, name: profile.name }} />}
        {stackOverflow ? (
          <ExternalLink href={stackOverflow}>View on Stack Overflow</ExternalLink>
        ) : null}
      </div>
    </header>
  );
}

export function ProfileFacts({ profile }: { profile: UserProfileWithCount }) {
  const website = safeExternalUrl(profile.website_url);
  const about = profile.about ? paragraphs(htmlToText(profile.about)) : [];
  const imported = profile.source === 'stackoverflow';
  return (
    <Card title="About" headingLevel={2}>
      {about.length > 0 ? (
        <div className={styles.about}>
          {about.map((paragraph, index) => (
            <p key={index}>{paragraph}</p>
          ))}
        </div>
      ) : null}
      <dl className={styles.facts}>
        {profile.location ? (
          <div>
            <dt>Location</dt>
            <dd>{profile.location}</dd>
          </div>
        ) : null}
        {profile.website_url ? (
          <div>
            <dt>Website</dt>
            <dd className={styles.breakable}>
              {website ? (
                <ExternalLink href={website}>{displayHost(website)}</ExternalLink>
              ) : (
                profile.website_url
              )}
            </dd>
          </div>
        ) : null}
        <div>
          <dt>{imported ? 'Joined Stack Overflow' : 'Joined SocialGraph'}</dt>
          <dd>
            <time dateTime={profile.created_at}>
              {imported ? formatMonthYear(profile.created_at) : formatDate(profile.created_at)}
            </time>
          </dd>
        </div>
        {profile.last_access_at ? (
          <div>
            <dt>Last seen</dt>
            <dd>
              <time dateTime={profile.last_access_at} title={formatDate(profile.last_access_at)}>
                {imported
                  ? formatMonthYear(profile.last_access_at)
                  : formatRelativeTime(profile.last_access_at)}
              </time>
            </dd>
          </div>
        ) : null}
        <div>
          <dt>Connections</dt>
          <dd>{formatInteger(profile.connection_count)}</dd>
        </div>
        {imported ? (
          <>
            <div>
              <dt>Profile views</dt>
              <dd>{formatInteger(profile.views)}</dd>
            </div>
            <div>
              <dt>Votes cast</dt>
              <dd>
                {formatInteger(profile.up_votes)} up · {formatInteger(profile.down_votes)} down
              </dd>
            </div>
          </>
        ) : null}
      </dl>
    </Card>
  );
}
