import { useId, type ReactNode } from 'react';
import styles from './Card.module.css';

interface CardProps {
  title?: ReactNode;
  /** Heading level for the title (sections inside a page use h2 by default). */
  headingLevel?: 2 | 3;
  description?: ReactNode;
  actions?: ReactNode;
  /** Extra controls rendered under the header (filters, segmented controls…). */
  toolbar?: ReactNode;
  footer?: ReactNode;
  children?: ReactNode;
  className?: string;
  /** Remove body padding (for full-bleed tables or canvases). */
  flush?: boolean;
  /** Set while a refetch is in progress so the previous render stays visible but dimmed. */
  busy?: boolean;
  id?: string;
}

/** A titled section. Renders a <section> labelled by its heading. */
export function Card({
  title,
  headingLevel = 2,
  description,
  actions,
  toolbar,
  footer,
  children,
  className,
  flush = false,
  busy = false,
  id,
}: CardProps) {
  const headingId = useId();
  const Heading = headingLevel === 2 ? 'h2' : 'h3';
  return (
    <section
      id={id}
      className={[styles.card, className].filter(Boolean).join(' ')}
      aria-labelledby={title ? headingId : undefined}
      aria-busy={busy || undefined}
    >
      {title || actions ? (
        <header className={styles.header}>
          <div className={styles.titles}>
            {title ? (
              <Heading id={headingId} className={styles.title} tabIndex={-1}>
                {title}
              </Heading>
            ) : null}
            {description ? <p className={styles.description}>{description}</p> : null}
          </div>
          {actions ? <div className={styles.actions}>{actions}</div> : null}
        </header>
      ) : null}
      {toolbar ? <div className={styles.toolbar}>{toolbar}</div> : null}
      <div className={[styles.body, flush ? styles.flush : '', busy ? styles.busy : ''].join(' ')}>
        {children}
      </div>
      {footer ? <footer className={styles.footer}>{footer}</footer> : null}
    </section>
  );
}
