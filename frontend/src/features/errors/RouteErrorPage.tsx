import { isRouteErrorResponse, Link, useRouteError } from 'react-router';
import { Button } from '../../components/Button';
import { Icon } from '../../components/Icon';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { NotFoundPage } from './NotFoundPage';
import styles from './ErrorPages.module.css';

/** A lazily loaded page whose file no longer exists (a new version was deployed meanwhile). */
function isChunkLoadError(error: unknown): boolean {
  return (
    error instanceof Error &&
    /dynamically imported module|Importing a module script failed|error loading dynamically/i.test(
      error.message,
    )
  );
}

/**
 * Router error boundary: unknown routes and unexpected render errors. `inShell` renders inside
 * the app shell's <main> (navigation stays usable); otherwise it is a standalone page.
 */
export function RouteErrorPage({ inShell = false }: { inShell?: boolean }) {
  const error = useRouteError();
  if (isRouteErrorResponse(error) && error.status === 404) return <NotFoundPage />;
  return <UnexpectedError error={error} standalone={!inShell} />;
}

interface UnexpectedErrorProps {
  error: unknown;
  /** Offers "Try again" (re-render) instead of a link home; used outside the router. */
  onReset?: () => void;
  standalone?: boolean;
}

export function UnexpectedError({ error, onReset, standalone = true }: UnexpectedErrorProps) {
  const outdated = isChunkLoadError(error);
  useDocumentTitle(outdated ? 'Update available' : 'Something went wrong');
  const content = (
    <div className={styles.wrapper} role="alert">
      <span className={styles.icon}>
        <Icon name="alert" size={28} />
      </span>
      <h1 className={styles.title}>
        {outdated ? 'A new version is available' : 'Something went wrong'}
      </h1>
      <p className={styles.message}>
        {outdated
          ? 'SocialGraph was updated while this page was open. Reload to continue.'
          : 'An unexpected error stopped this page from loading. Reloading usually fixes it.'}
      </p>
      <div className={styles.actions}>
        <Button
          variant="primary"
          icon="refresh"
          onClick={() => {
            window.location.reload();
          }}
        >
          Reload
        </Button>
        {onReset ? (
          <Button onClick={onReset}>Try again</Button>
        ) : (
          <Link to="/" className={styles.homeLink}>
            Go to the overview
          </Link>
        )}
      </div>
    </div>
  );
  return standalone ? <main className={styles.standalone}>{content}</main> : content;
}
