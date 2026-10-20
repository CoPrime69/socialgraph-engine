import { Suspense, useEffect, useRef } from 'react';
import { Link, NavLink, Outlet, useLocation } from 'react-router';
import { useSession, useAuth } from '../auth/authContext';
import { Button } from '../components/Button';
import { Icon, type IconName } from '../components/Icon';
import { PageSkeleton } from '../components/PageSkeleton';
import { Avatar } from '../components/UserChip';
import { ProfileGate } from '../features/profile/ProfileGate';
import { Logo } from './Logo';
import styles from './AppShell.module.css';

interface NavItem {
  to: string;
  label: string;
  icon: IconName;
  end?: boolean;
}

const NAV_ITEMS: readonly NavItem[] = [
  { to: '/', label: 'Overview', icon: 'overview', end: true },
  { to: '/network', label: 'My network', icon: 'network' },
  { to: '/communities', label: 'Communities', icon: 'communities' },
  { to: '/people', label: 'People', icon: 'people' },
  { to: '/topics', label: 'Topics', icon: 'topics' },
];

export function AppShell() {
  const session = useSession();
  const { signOut } = useAuth();
  const location = useLocation();
  const mainRef = useRef<HTMLElement>(null);
  const firstRender = useRef(true);

  // Move focus to the new page on client-side navigation so keyboard and screen-reader users
  // start at the top of the new content (not on the link they just activated).
  useEffect(() => {
    if (firstRender.current) {
      firstRender.current = false;
      return;
    }
    mainRef.current?.focus({ preventScroll: true });
    window.scrollTo({ top: 0 });
  }, [location.pathname]);

  return (
    <div className={styles.shell}>
      <a className="skip-link" href="#main">
        Skip to main content
      </a>
      <header className={styles.header}>
        <Link to="/" className={styles.brand} aria-label="SocialGraph home">
          <Logo />
          <span className={styles.brandName}>SocialGraph</span>
        </Link>
        <div className={styles.account}>
          <Link to={`/users/${session.user.id}`} className={styles.user}>
            <Avatar name={session.user.name} size="sm" highlighted />
            <span className={styles.userText}>
              <span className={styles.userName}>{session.user.name}</span>{' '}
              <span className={styles.userHandle}>@{session.user.handle}</span>
            </span>
          </Link>
          <Button variant="ghost" icon="signOut" onClick={signOut} className={styles.signOut}>
            <span className={styles.signOutLabel}>Sign out</span>
          </Button>
        </div>
      </header>

      <nav className={styles.nav} aria-label="Main">
        <ul role="list" className={styles.navList}>
          {NAV_ITEMS.map((item) => (
            <li key={item.to}>
              <NavLink
                to={item.to}
                end={item.end ?? false}
                className={({ isActive }) =>
                  [styles.navLink, isActive ? styles.active : ''].join(' ')
                }
              >
                <Icon name={item.icon} size={20} />
                <span>{item.label}</span>
              </NavLink>
            </li>
          ))}
        </ul>
      </nav>

      <main id="main" ref={mainRef} tabIndex={-1} className={styles.main}>
        <div className={styles.content}>
          <ProfileGate>
            <Suspense fallback={<PageSkeleton />}>
              <Outlet />
            </Suspense>
          </ProfileGate>
        </div>
      </main>
    </div>
  );
}
