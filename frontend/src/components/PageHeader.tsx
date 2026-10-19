import type { ReactNode } from 'react';
import styles from './PageHeader.module.css';

interface PageHeaderProps {
  title: ReactNode;
  description?: ReactNode;
  /** Small secondary line (e.g. graph version / computed time). */
  meta?: ReactNode;
  actions?: ReactNode;
  /** Content above the title (e.g. a back link). */
  before?: ReactNode;
}

export function PageHeader({ title, description, meta, actions, before }: PageHeaderProps) {
  return (
    <header className={styles.header}>
      {before ? <div className={styles.before}>{before}</div> : null}
      <div className={styles.row}>
        <div className={styles.titles}>
          <h1 className={styles.title}>{title}</h1>
          {description ? <p className={styles.description}>{description}</p> : null}
          {meta ? <p className={styles.meta}>{meta}</p> : null}
        </div>
        {actions ? <div className={styles.actions}>{actions}</div> : null}
      </div>
    </header>
  );
}
