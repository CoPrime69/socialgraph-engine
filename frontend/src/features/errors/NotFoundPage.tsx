import { ButtonLink } from '../../components/Button';
import { Icon } from '../../components/Icon';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import styles from './ErrorPages.module.css';

interface NotFoundPageProps {
  title?: string;
  message?: string;
}

export function NotFoundPage({
  title = 'Page not found',
  message = 'The page you are looking for does not exist or has moved.',
}: NotFoundPageProps) {
  useDocumentTitle(title);
  return (
    <div className={styles.wrapper}>
      <span className={styles.icon}>
        <Icon name="search" size={28} />
      </span>
      <h1 className={styles.title}>{title}</h1>
      <p className={styles.message}>{message}</p>
      <div className={styles.actions}>
        <ButtonLink to="/" variant="primary">
          Go to the overview
        </ButtonLink>
        <ButtonLink to="/people">Find people</ButtonLink>
      </div>
    </div>
  );
}
