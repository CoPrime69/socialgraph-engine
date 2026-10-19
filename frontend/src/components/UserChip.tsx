import type { ReactNode } from 'react';
import { Link } from 'react-router';
import { initials } from '../lib/text';
import styles from './UserChip.module.css';

interface AvatarProps {
  name: string;
  size?: 'sm' | 'md' | 'lg';
  highlighted?: boolean;
}

/** Initials avatar (no remote images: the CSP only allows same-origin/data: images). */
export function Avatar({ name, size = 'md', highlighted = false }: AvatarProps) {
  return (
    <span
      className={[styles.avatar, styles[size], highlighted ? styles.highlighted : ''].join(' ')}
      aria-hidden="true"
    >
      {initials(name)}
    </span>
  );
}

export interface ChipUser {
  id: number;
  name: string;
  handle: string;
}

interface UserChipProps {
  user: ChipUser;
  /** Secondary line; defaults to "@handle". */
  subtitle?: ReactNode;
  /** Link to the profile (default) or render as plain text with `link={false}`. */
  link?: boolean;
  variant?: 'inline' | 'pill';
  /** Marks the signed-in user. */
  isYou?: boolean;
  size?: 'sm' | 'md';
}

export function UserChip({
  user,
  subtitle,
  link = true,
  variant = 'inline',
  isYou = false,
  size = 'md',
}: UserChipProps) {
  const label = (
    <>
      <span className={styles.name}>{isYou ? 'You' : user.name}</span>
      {variant === 'inline' ? (
        <>
          {' '}
          <span className={styles.subtitle}>{subtitle ?? `@${user.handle}`}</span>
        </>
      ) : null}
    </>
  );
  const content = (
    <>
      <Avatar
        name={user.name}
        size={size === 'sm' || variant === 'pill' ? 'sm' : 'md'}
        highlighted={isYou}
      />
      <span className={styles.text}>{label}</span>
    </>
  );
  const className = [styles.chip, styles[variant], isYou ? styles.you : ''].join(' ');
  if (!link) return <span className={className}>{content}</span>;
  return (
    <Link
      to={`/users/${user.id}`}
      className={className}
      aria-label={variant === 'pill' && !isYou ? `${user.name} (@${user.handle})` : undefined}
    >
      {content}
    </Link>
  );
}
